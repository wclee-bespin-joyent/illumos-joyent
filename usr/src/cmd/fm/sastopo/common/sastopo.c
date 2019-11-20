/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright 2019 Joyent, Inc.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fnmatch.h>
#include <libnvpair.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <fm/libtopo.h>
#include <fm/topo_list.h>
#include <fm/topo_sas.h>
#include <sys/fm/protocol.h>
#include <topo_prop.h>
#include <topo_tree.h>

#define	EXIT_USAGE	2

static const char *pname;
static const char optstr[] = "Cdf:hpR:Vx";

static void
usage()
{
	(void) fprintf(stderr,
	    "Usage:\n\n"
	    "Print all nodes on the SAS fabric:\n"
	    "  %s [-d][-V][-R root][FMRI pattern]\n\n"
	    "Print all the paths between SAS initiators and targets:\n"
	    "  %s -p [-d][-R root]\n\n"
	    "Dump SAS topology to XML\n"
	    "  %s -x [-d][-R root]\n\n"
	    "Read in SAS topology from XML\n"
	    "  %s -f <XML file> [-d][-R root]\n\n"
	    "-C\t\tdump core at exit\n"
	    "-d\t\tenable debug messages\n"
	    "-h\t\tprint this usage message\n"
	    "-R\t\toperate against alternate root directory\n"
	    "-V\t\tverbose mode\n\n", pname, pname, pname, pname);
}

struct sastopo_vertex {
	topo_list_t link;
	topo_vertex_t *vtx;
};

struct cb_arg {
	topo_list_t ini_list;
	topo_list_t tgt_list;
	boolean_t verbose;
	boolean_t do_paths;
	const char *fmri_pattern;
};

static int
print_prop_val(topo_hdl_t *thp, tnode_t *tn, topo_propval_t *pv,
    const char *pgname)
{
	topo_type_t type = pv->tp_type;
	const char *pname = pv->tp_name;
	int err;

	switch (type) {
		case TOPO_TYPE_INT32: {
			int32_t val;

			if (topo_prop_get_int32(tn, pgname, pname, &val, &err)
			    != 0)
				return (-1);

			(void) printf("%-10u\n", val);
			break;
		}
		case TOPO_TYPE_UINT32: {
			uint32_t val;

			if (topo_prop_get_uint32(tn, pgname, pname, &val, &err)
			    != 0)
				return (-1);

			(void) printf("%-10d\n", val);
			break;
		}
		case TOPO_TYPE_INT64: {
			int64_t val;

			if (topo_prop_get_int64(tn, pgname, pname, &val, &err)
			    != 0)
				return (-1);

			(void) printf("%-10" PRIi64 "\n", val);
			break;
		}
		case TOPO_TYPE_UINT64: {
			uint64_t val;

			if (topo_prop_get_uint64(tn, pgname, pname, &val, &err)
			    != 0) {
				(void) printf("%s", topo_strerror(err));
				return (-1);
			}

			(void) printf("0x%-10" PRIx64 "\n", val);
			break;
		}
		case TOPO_TYPE_STRING: {
			char *val;

			if (topo_prop_get_string(tn, pgname, pname, &val, &err)
			    != 0)
				return (-1);

			(void) printf("%-10s\n", val);
			break;
		}
		case TOPO_TYPE_FMRI: {
			nvlist_t *nvl;
			char *fmri;

			if (topo_prop_get_fmri(tn, pgname, pname, &nvl, &err)
			    != 0)
				return (-1);

			if (topo_fmri_nvl2str(thp, nvl, &fmri, &err) != 0) {
				nvlist_print(stdout, nvl);
				break;
			}
			(void) printf("%10s\n", fmri);
			topo_hdl_strfree(thp, fmri);
			break;
		}
		case TOPO_TYPE_INT32_ARRAY: {
			uint_t nelem;
			int32_t *val;

			if (topo_prop_get_int32_array(tn, pgname, pname, &val,
			    &nelem, &err) != 0)
				return (-1);

			(void) printf(" [ ");
			for (uint_t i = 0; i < nelem; i++)
				(void) printf("%d ", val[i]);

			(void) printf("]\n");

			break;
		}
		case TOPO_TYPE_UINT32_ARRAY: {
			uint_t nelem;
			uint32_t *val;

			if (topo_prop_get_uint32_array(tn, pgname, pname, &val,
			    &nelem, &err) != 0)
				return (-1);

			(void) printf(" [ ");
			for (uint_t i = 0; i < nelem; i++)
				(void) printf("%u ", val[i]);
			(void) printf("]\n");
			break;
		}
		case TOPO_TYPE_INT64_ARRAY: {
			uint_t nelem;
			int64_t *val;

			if (topo_prop_get_int64_array(tn, pgname, pname, &val,
			    &nelem, &err) != 0)
				return (-1);

			(void) printf(" [ ");
			for (uint_t i = 0; i < nelem; i++)
				(void) printf("%" PRIi64 " ", val[i]);
			(void) printf("]\n");

			break;
		}
		case TOPO_TYPE_UINT64_ARRAY: {
			uint_t nelem;
			uint64_t *val;

			if (topo_prop_get_uint64_array(tn, pgname, pname, &val,
			    &nelem, &err) != 0)
				return (-1);

			(void) printf(" [ ");
			for (uint_t i = 0; i < nelem; i++)
				(void) printf("0x%" PRIx64 " ", val[i]);
			(void) printf("]\n");

			break;
		}
		case TOPO_TYPE_STRING_ARRAY: {
			uint_t nelem;
			char **val;

			if (topo_prop_get_string_array(tn, pgname, pname, &val,
			    &nelem, &err) != 0)
				return (-1);

			(void) printf(" [ ");
			for (uint_t i = 0; i < nelem; i++)
				(void) printf("\"%s\" ", val[i]);
			(void) printf("]\n");

			break;
		}
		default:
			(void) fprintf(stderr, "Invalid nvpair data type: %d\n",
			    type);
			return (-1);
	}
	return (0);
}

static void
print_node_props(topo_hdl_t *thp, tnode_t *tn)
{
	topo_pgroup_t *pg;

	for (pg = topo_list_next(&tn->tn_pgroups); pg != NULL;
	    pg = topo_list_next(pg)) {

		topo_proplist_t *pvl;

		(void) printf("  %-8s: %s\n",  "group", pg->tpg_info->tpi_name);

		for (pvl = topo_list_next(&pg->tpg_pvals); pvl != NULL;
		    pvl = topo_list_next(pvl)) {

			topo_propval_t *pv = pvl->tp_pval;
			char *tstr;

			switch (pv->tp_type) {
			case TOPO_TYPE_BOOLEAN: tstr = "boolean"; break;
			case TOPO_TYPE_INT32: tstr = "int32"; break;
			case TOPO_TYPE_UINT32: tstr = "uint32"; break;
			case TOPO_TYPE_INT64: tstr = "int64"; break;
			case TOPO_TYPE_UINT64: tstr = "uint64"; break;
			case TOPO_TYPE_DOUBLE: tstr = "double"; break;
			case TOPO_TYPE_STRING: tstr = "string"; break;
			case TOPO_TYPE_FMRI: tstr = "fmri"; break;
			case TOPO_TYPE_INT32_ARRAY: tstr = "int32[]"; break;
			case TOPO_TYPE_UINT32_ARRAY: tstr = "uint32[]"; break;
			case TOPO_TYPE_INT64_ARRAY: tstr = "int64[]"; break;
			case TOPO_TYPE_UINT64_ARRAY: tstr = "uint64[]"; break;
			case TOPO_TYPE_STRING_ARRAY: tstr = "string[]"; break;
			case TOPO_TYPE_FMRI_ARRAY: tstr = "fmri[]"; break;
			default: tstr = "unknown type";
			}

			(void) printf("    %-20s %-10s", pv->tp_name, tstr);

			if (print_prop_val(thp, tn, pv, pg->tpg_info->tpi_name)
			    != 0) {
				(void) printf("failed to get prop val!\n");
			}
		}
	}
	(void) printf("\n");
}

static void
print_vertex(topo_hdl_t *thp, topo_vertex_t *vtx, struct cb_arg *cbarg)
{
	tnode_t *tn;
	nvlist_t *fmri = NULL;
	char *fmristr = NULL;
	int err;

	/*
	 * Generate a string representation of the FMRI.
	 */
	tn = topo_vertex_node(vtx);
	if (topo_node_resource(tn, &fmri, &err) != 0 ||
	    topo_fmri_nvl2str(thp, fmri, &fmristr, &err) != 0) {
		(void) fprintf(stderr, "failed to convert FMRI for %s=%"
		    PRIx64 " to a string\n", topo_node_name(tn),
		    topo_node_instance(tn));
		nvlist_print(stderr, fmri);
		goto out;
	}

	/*
	 * If an FMRI pattern was specified on the command line, then check if
	 * this node matches that pattern.  If not, skip printing it.
	 */
	if (cbarg->fmri_pattern != NULL &&
	    fnmatch(cbarg->fmri_pattern, fmristr, 0) != 0) {
		goto out;
	}

	(void) printf("%s\n", fmristr);
	if (cbarg->verbose)
		print_node_props(thp, tn);
out:
	topo_hdl_strfree(thp, fmristr);
	nvlist_free(fmri);
}

static void
print_path(topo_path_t *path)
{
	(void) printf("%s\n", path->tsp_fmristr);
}

static int
vertex_cb(topo_hdl_t *thp, topo_vertex_t *vtx, boolean_t last_vtx,
    void *arg)
{
	struct cb_arg *cbarg = arg;
	tnode_t *tn = topo_vertex_node(vtx);
	struct sastopo_vertex *sasvtx;

	if (!cbarg->do_paths)
		print_vertex(thp, vtx, cbarg);

	if (strcmp(topo_node_name(tn), TOPO_VTX_INITIATOR) != 0 &&
	    strcmp(topo_node_name(tn), TOPO_VTX_TARGET) != 0) {
		return (TOPO_WALK_NEXT);
	}
	if ((sasvtx = topo_hdl_zalloc(thp,
	    sizeof (struct sastopo_vertex))) == NULL) {
		return (TOPO_WALK_ERR);
	}
	sasvtx->vtx = vtx;

	if (strcmp(topo_node_name(tn), TOPO_VTX_INITIATOR) == 0) {
		topo_list_append(&cbarg->ini_list, sasvtx);
	} else if (strcmp(topo_node_name(tn), TOPO_VTX_TARGET) == 0) {
		topo_list_append(&cbarg->tgt_list, sasvtx);
	}
	return (TOPO_WALK_NEXT);
}

int
main(int argc, char *argv[])
{
	topo_hdl_t *thp = NULL;
	topo_digraph_t *tdg;
	char c, *root = "/", *xml_in = NULL, *buf;
	boolean_t debug = B_FALSE, xml_out = B_FALSE;
	int err, fd, status = EXIT_FAILURE;
	struct stat statbuf = { 0 };
	struct cb_arg cbarg = { 0 };
	struct sastopo_vertex *ini, *tgt;

	pname = argv[0];

	while (optind < argc) {
		while ((c = getopt(argc, argv, optstr)) != -1) {
			switch (c) {
			case 'C':
				(void) atexit(abort);
				break;
			case 'd':
				debug = B_TRUE;
				break;
			case 'f':
				xml_in = optarg;
				break;
			case 'h':
				usage();
				return (EXIT_USAGE);
			case 'p':
				cbarg.do_paths = B_TRUE;
				break;
			case 'R':
				root = optarg;
				break;
			case 'V':
				cbarg.verbose = B_TRUE;
				break;
			case 'x':
				xml_out = B_TRUE;
				break;
			default:
				usage();
				return (EXIT_USAGE);
			}
		}
		if (optind < argc) {
			if (cbarg.fmri_pattern != NULL) {
				usage();
				return (EXIT_USAGE);
			} else {
				cbarg.fmri_pattern = argv[optind++];
			}
		}
	}

	if (xml_out && cbarg.do_paths) {
		(void) fprintf(stderr, "-x and -p are mutually exclusive\n");
		usage();
		return (EXIT_USAGE);
	}

	if (debug) {
		if (putenv("TOPOSASDEBUG=1") != 0) {
			(void) fprintf(stderr, "Failed to set debug mode: "
			    "%s\n", strerror(errno));
			goto out;
		}
	}
	/*
	 * If we're loading in a serialized snapshot, then we need to tell the
	 * sas module not to construct a snapshot.  The tmo_enum entry
	 * point in the sas module will check for this environment variable
	 * and if found it will skip enuemrating the actual SAS fabric.
	 */
	if (xml_in != NULL) {
		if (putenv("TOPO_SASNOENUM=1") != 0) {
			(void) fprintf(stderr, "Failed to set xml_in mode: "
			    "%s\n", strerror(errno));
			goto out;
		}
	}
	if ((thp = topo_open(TOPO_VERSION, root, &err)) == NULL) {
		(void) fprintf(stderr, "failed to get topo handle: %s\n",
		    topo_strerror(err));
		goto out;
	}
	if (debug)
		topo_debug_set(thp, "module", "stderr");

	if (topo_snap_hold(thp, NULL, &err) == NULL) {
		(void) fprintf(stderr, "failed to take topo snapshot: %s\n",
		    topo_strerror(err));
		goto out;
	}

	/*
	 * Either get a pointer to a rehydrated "sas" scheme digraph or get a
	 * pointer to the live "sas" scheme digraph.
	 */
	if (xml_in != NULL) {
		if ((fd = open(xml_in, O_RDONLY)) < 0) {
			(void) fprintf(stderr, "failed to open %s (%s)\n",
			    xml_in, strerror(errno));
			goto out;
		}
		if (fstat(fd, &statbuf) != 0) {
			(void) fprintf(stderr, "failed to stat %s (%s)\n",
			    xml_in, strerror(errno));
			(void) close(fd);
			goto out;
		}
		if ((buf = malloc(statbuf.st_size)) == NULL) {
			(void) fprintf(stderr, "failed to alloc read buffer: "
			    "(%s)\n", strerror(errno));
			(void) close(fd);
			goto out;
		}
		if (read(fd, buf, statbuf.st_size) != statbuf.st_size) {
			(void) fprintf(stderr, "failed to read file: "
			    "(%s)\n", strerror(errno));
			(void) close(fd);
			free(buf);
			goto out;
		}
		(void) close(fd);

		tdg = topo_digraph_deserialize(thp, buf, statbuf.st_size);
		free(buf);
	} else {
		tdg = topo_digraph_get(thp, FM_FMRI_SCHEME_SAS);
	}

	if (tdg == NULL) {
		(void) fprintf(stderr, "failed to get sas scheme digraph\n");
		goto out;
	}

	/*
	 * If -j was passed then we're just going to dump a JSON version of the
	 * digraph and then exit.
	 */
	if (xml_out) {
		if (topo_digraph_serialize(thp, tdg, stdout) == 0)
			status = EXIT_SUCCESS;
		else
			(void) fprintf(stderr, "failed to serialize "
			    "topology\n");

		goto out;
	}

	/*
	 * Iterate through the print all of the vertices.  While iterating we
	 * also generate a list of initiators and a list of targets.
	 */
	if (topo_vertex_iter(thp, tdg, vertex_cb, &cbarg) != 0) {
		(void) fprintf(stderr, "failed to iterate vertices\n");
		goto out;
	}

	if (!cbarg.do_paths) {
		status = EXIT_SUCCESS;
		goto out;
	}

	/*
	 * Find and print all unique paths between the initiators and
	 * targets.
	 */
	for (ini = topo_list_next(&cbarg.ini_list); ini != NULL;
	    ini = topo_list_next(ini)) {
		for (tgt = topo_list_next(&cbarg.tgt_list); tgt != NULL;
		    tgt = topo_list_next(tgt)) {
			int np;
			topo_path_t **paths;

			np = topo_digraph_paths(thp, tdg, ini->vtx, tgt->vtx,
			    &paths);
			if (np < 0) {
				(void) fprintf(stderr, "topo_digraph_paths "
				    "failed!\n");
				goto out;
			} else if (np == 0) {
				tnode_t *ti, *tt;

				ti = topo_vertex_node(ini->vtx);
				tt = topo_vertex_node(tgt->vtx);
				if (debug) {
					(void) fprintf(stderr, "failed to find "
					    "path between initiator=%" PRIx64
					    " and target=%" PRIx64 "\n",
					    topo_node_instance(ti),
					    topo_node_instance(tt));
				}
				continue;
			}
			for (uint_t i = 0; i < np; i++)
				print_path(paths[i]);

			topo_hdl_free(thp, paths, np * sizeof (topo_path_t *));
		}
	}
	status = EXIT_SUCCESS;
out:
	if (thp != NULL)  {
		topo_snap_release(thp);
		topo_close(thp);
	}
	return (status);
}
