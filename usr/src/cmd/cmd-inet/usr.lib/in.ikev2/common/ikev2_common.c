/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Copyright (c) 2017, Joyent, Inc.
 */
#include <sys/types.h>
#include <net/pfkeyv2.h>
#include <sys/debug.h>
#include <string.h>
#include "defs.h"
#include "ikev2_sa.h"
#include "ikev2_pkt.h"
#include "ikev2_common.h"
#include "ikev2_enum.h"
#include "ikev2_proto.h"
#include "config.h"
#include "pkcs11.h"
#include "pkt.h"

/*
 * XXX: IKEv1 selected the PRF based on the authentication algorithm.
 * IKEv2 allows the PRF to be negotiated separately.  Eventually, we
 * should probably add the ability to specify PRFs in the configuration
 * file.  For now, we just include all the ones we support in decreasing
 * order of preference.
 */
static ikev2_prf_t prf_supported[] = {
	IKEV2_PRF_HMAC_SHA2_512,
	IKEV2_PRF_HMAC_SHA2_384,
	IKEV2_PRF_HMAC_SHA2_256,
	IKEV2_PRF_HMAC_SHA1,
	IKEV2_PRF_HMAC_MD5
};

boolean_t
ikev2_sa_from_acquire(pkt_t *restrict pkt, parsedmsg_t *restrict pmsg,
    uint32_t spi, ikev2_dh_t dh)
{
	sadb_msg_t *samsg = pmsg->pmsg_samsg;
	sadb_sa_t *sa;
	sadb_prop_t *prop;
	sadb_comb_t *comb;
	boolean_t ok;
	ikev2_spi_proto_t spi_type = IKEV2_PROTO_NONE;
	pkt_sa_state_t pss;

	ASSERT3U(samsg->sadb_msg_type, ==, SADB_ACQUIRE);

	switch (samsg->sadb_msg_satype) {
	case SADB_SATYPE_AH:
		spi_type = IKEV2_PROTO_AH;
		break;
	case SADB_SATYPE_ESP:
		spi_type = IKEV2_PROTO_ESP;
		break;
	default:
		INVALID("sadb_msg_satype");
	}

	prop = (sadb_prop_t *)pmsg->pmsg_exts[SADB_EXT_PROPOSAL];
	ASSERT3U(prop->sadb_prop_exttype, ==, SADB_EXT_PROPOSAL);

	ok = ikev2_add_sa(pkt, &pss);

	comb = (sadb_comb_t *)(prop + 1);
	for (size_t i = 0; i < prop->sadb_x_prop_numecombs; i++, comb++) {
		ok &= ikev2_add_prop(&pss, i + 1, spi_type, spi);

		if (comb->sadb_comb_encrypt != SADB_EALG_NONE) {
			ikev2_xf_encr_t encr;
			uint16_t minbits, maxbits;

			encr = ikev2_pfkey_to_encr(comb->sadb_comb_encrypt);
			minbits = comb->sadb_comb_encrypt_minbits;
			maxbits = comb->sadb_comb_encrypt_maxbits;
			ok &= ikev2_add_xf_encr(&pss, encr, minbits, maxbits);
		}

		if (comb->sadb_comb_auth != SADB_AALG_NONE) {
			ikev2_xf_auth_t xf_auth;
			/*
			 * Neither the auth algorithms currently supported
			 * nor the IKE protocol itself supports specifying
			 * a key/bits size for the auth alg.
			 */
			VERIFY3U(comb->sadb_comb_auth_minbits, ==, 0);
			VERIFY3U(comb->sadb_comb_auth_maxbits, ==, 0);

			xf_auth = ikev2_pfkey_to_auth(comb->sadb_comb_auth);
			ok &= ikev2_add_xform(&pss, IKEV2_XF_AUTH, xf_auth);
		}

		if (dh != IKEV2_DH_NONE)
			ok &= ikev2_add_xform(&pss, IKEV2_XF_DH, dh);

		/* We currently don't support ESNs */
		ok &= ikev2_add_xform(&pss, IKEV2_XF_ESN, IKEV2_ESN_NONE);
	}

	return (ok);
}

ikev2_xf_auth_t
ikev2_pfkey_to_auth(int alg)
{
	switch (alg) {
	case SADB_AALG_NONE:
	case SADB_AALG_SHA256HMAC:
	case SADB_AALG_SHA384HMAC:
	case SADB_AALG_SHA512HMAC:
		/* these values all correspond */
		return (alg);
	case SADB_AALG_MD5HMAC:
		/* this one does not */
		return (IKEV2_XF_AUTH_HMAC_MD5_96);
	case SADB_AALG_SHA1HMAC:
		/* nor does this one */
		return (IKEV2_XF_AUTH_HMAC_SHA1_96);
	default:
		INVALID("alg");
		/*NOTREACHED*/
		return (alg);
	}
}

ikev2_xf_encr_t
ikev2_pfkey_to_encr(int alg)
{
	switch (alg) {
	case SADB_EALG_NONE:
	case SADB_EALG_DESCBC:
	case SADB_EALG_3DESCBC:
	case SADB_EALG_BLOWFISH:
	case SADB_EALG_NULL:
	case SADB_EALG_AES:	/* CBC */
	case SADB_EALG_AES_CCM_8:
	case SADB_EALG_AES_CCM_12:
	case SADB_EALG_AES_CCM_16:
	case SADB_EALG_AES_GCM_8:
	case SADB_EALG_AES_GCM_12:
	case SADB_EALG_AES_GCM_16:
		return (alg);
	default:
		INVALID("alg");
		/*NOTREACHED*/
		return (alg);
	}
}

static boolean_t add_rule_xform(pkt_sa_state_t *restrict,
    const config_xf_t *restrict);

boolean_t
ikev2_sa_from_rule(pkt_t *restrict pkt, const config_rule_t *restrict rule,
    uint64_t spi)
{
	boolean_t ok = B_TRUE;
	pkt_sa_state_t pss;

	if (!ikev2_add_sa(pkt, &pss))
		return (B_FALSE);

	for (uint8_t i = 0; rule->rule_xf[i] != NULL; i++) {
		/* RFC 7296 3.3.1 - Proposal numbers start with 1 */
		ok &= ikev2_add_prop(&pss, i + 1, IKEV2_PROTO_IKE, spi);
		ok &= add_rule_xform(&pss, rule->rule_xf[i]);
	}
	return (ok);
}

static boolean_t
add_rule_xform(pkt_sa_state_t *restrict pss, const config_xf_t *restrict xf)
{
	encr_modes_t mode = encr_data[xf->xf_encr].ed_mode;
	boolean_t ok = B_TRUE;

	ok &= ikev2_add_xf_encr(pss, xf->xf_encr, xf->xf_minbits,
	    xf->xf_maxbits);

	/*
	 * For all currently known combined mode ciphers, we can omit an
	 * integrity transform
	 */
	if (!MODE_IS_COMBINED(mode))
		ok &= ikev2_add_xform(pss, IKEV2_XF_AUTH, xf->xf_auth);
	ok &= ikev2_add_xform(pss, IKEV2_XF_DH, xf->xf_dh);

	for (size_t i = 0; ok && i < ARRAY_SIZE(prf_supported); i++)
		ok &= ikev2_add_xform(pss, IKEV2_XF_PRF, prf_supported[i]);

	return (ok);
}

boolean_t
ikev2_sa_add_result(pkt_t *restrict pkt,
    const ikev2_sa_result_t *restrict result)
{
	boolean_t ok;
	pkt_sa_state_t pss;

	ok = ikev2_add_sa(pkt, &pss);
	ok &= ikev2_add_prop(&pss, result->sar_propnum, result->sar_proto,
	    result->sar_spi);

	if (SA_RESULT_HAS(result, IKEV2_XF_ENCR)) {
		ok &= ikev2_add_xform(&pss, IKEV2_XF_ENCR, result->sar_encr);
		if (result->sar_encr_keylen != 0)
			ok &= ikev2_add_xf_attr(&pss, IKEV2_XF_ATTR_KEYLEN,
			    result->sar_encr_keylen);
	}
	if (SA_RESULT_HAS(result, IKEV2_XF_AUTH))
		ok &= ikev2_add_xform(&pss, IKEV2_XF_AUTH, result->sar_auth);
	if (SA_RESULT_HAS(result, IKEV2_XF_DH))
		ok &= ikev2_add_xform(&pss, IKEV2_XF_DH, result->sar_dh);
	if (SA_RESULT_HAS(result, IKEV2_XF_PRF))
		ok &= ikev2_add_xform(&pss, IKEV2_XF_PRF, result->sar_prf);
	if (SA_RESULT_HAS(result, IKEV2_XF_ESN))
		ok &= ikev2_add_xform(&pss, IKEV2_XF_ESN, result->sar_esn);

	return (ok);
}

struct rule_data_s {
	bunyan_logger_t		*rd_log;
	config_rule_t		*rd_rule;
	config_xf_t		*rd_xf;
	ikev2_sa_result_t	*rd_res;
	ikev2_prf_t		rd_prf;
	boolean_t		rd_match;
	boolean_t		rd_skip;
	boolean_t		rd_has_auth;
	boolean_t		rd_keylen_match;
};

static boolean_t match_rule_prop_cb(ikev2_sa_proposal_t *, uint64_t, uint8_t *,
    size_t, void *);
static boolean_t match_rule_xf_cb(ikev2_transform_t *, uint8_t *, size_t,
    void *);
static boolean_t match_rule_attr_cb(ikev2_attribute_t *, void *);

boolean_t
ikev2_sa_match_rule(config_rule_t *restrict rule, pkt_t *restrict pkt,
    ikev2_sa_result_t *restrict result)
{
	pkt_payload_t *pay = pkt_get_payload(pkt, IKEV2_PAYLOAD_SA, NULL);
	bunyan_logger_t *l = pkt->pkt_sa->i2sa_log;

	VERIFY3P(pay, !=, NULL);

	bunyan_debug(l, "Checking rules against proposals",
	    BUNYAN_T_STRING, "rule", rule->rule_label,
	    BUNYAN_T_END);

	for (size_t i = 0; rule->rule_xf[i] != NULL; i++) {
		for (size_t j = 0; j < ARRAY_SIZE(prf_supported); j++) {
			struct rule_data_s data = {
				.rd_log = l,
				.rd_rule = rule,
				.rd_xf = rule->rule_xf[i],
				.rd_res = result,
				.rd_prf = prf_supported[j],
				.rd_match = B_FALSE
			};

			(void) memset(result, 0, sizeof (*result));

			bunyan_trace(l,
			    "Checking rule transform against proposals",
			    BUNYAN_T_UINT32, "xfnum", (uint32_t)i,
			    BUNYAN_T_STRING, "xf", rule->rule_xf[i]->xf_str,
			    BUNYAN_T_END);

			VERIFY(ikev2_walk_proposals(pay->pp_ptr, pay->pp_len,
			    match_rule_prop_cb, &data, l));

			if (data.rd_match) {
				bunyan_debug(l, "Found proposal match",
				    BUNYAN_T_STRING, "xf",
				    rule->rule_xf[i]->xf_str,
				    BUNYAN_T_UINT32, "propnum",
				    (uint32_t)result->sar_propnum,
				    BUNYAN_T_UINT64, "spi", result->sar_spi,
				    BUNYAN_T_STRING, "encr",
				    ikev2_xf_encr_str(result->sar_encr),
				    BUNYAN_T_UINT32, "keylen",
				    (uint32_t)result->sar_encr_keylen,
				    BUNYAN_T_STRING, "auth",
				    ikev2_xf_auth_str(result->sar_auth),
				    BUNYAN_T_STRING, "prf",
				    ikev2_prf_str(result->sar_prf),
				    BUNYAN_T_STRING, "dh",
				    ikev2_dh_str(result->sar_dh),
				    BUNYAN_T_END);
				return (B_TRUE);
			}
		}
	}

	bunyan_debug(l, "No matching proposals found", BUNYAN_T_END);
	return (B_FALSE);
}

static boolean_t
match_rule_prop_cb(ikev2_sa_proposal_t *prop, uint64_t spi, uint8_t *buf,
    size_t buflen, void *cookie)
{
	struct rule_data_s *data = cookie;

	bunyan_trace(data->rd_log, "Checking proposal",
	    BUNYAN_T_UINT32, "propnum", (uint32_t)prop->proto_proposalnr,
	    BUNYAN_T_END);

	if (prop->proto_protoid != IKEV2_PROTO_IKE) {
		bunyan_trace(data->rd_log, "Proposal is not for IKE",
		    BUNYAN_T_STRING, "protocol",
		    ikev2_spi_str(prop->proto_protoid),
		    BUNYAN_T_END);
		return (B_FALSE);
	}

	(void) memset(data->rd_res, 0, sizeof (*data->rd_res));
	data->rd_skip = B_FALSE;
	data->rd_has_auth = B_FALSE;

	VERIFY(ikev2_walk_xfs(buf, buflen, match_rule_xf_cb, cookie,
	    data->rd_log));

	if (data->rd_skip)
		return (B_TRUE);

	/* These must all match, otherwise next proposal */
	if (!SA_RESULT_HAS(data->rd_res, IKEV2_XF_ENCR) ||
	    !SA_RESULT_HAS(data->rd_res, IKEV2_XF_PRF) ||
	    !SA_RESULT_HAS(data->rd_res, IKEV2_XF_DH) ||
	    (!MODE_IS_COMBINED(encr_data[data->rd_res->sar_encr].ed_mode) &&
	    !SA_RESULT_HAS(data->rd_res, IKEV2_XF_AUTH)))
		return (B_TRUE);

	/* A match.  Stop walk of remaining proposals */
	data->rd_res->sar_proto = prop->proto_protoid;
	data->rd_res->sar_spi = spi;
	data->rd_res->sar_propnum = prop->proto_proposalnr;
	data->rd_match = B_TRUE;
	return (B_FALSE);
}

static boolean_t
match_rule_xf_cb(ikev2_transform_t *xf, uint8_t *buf, size_t buflen,
    void *cookie)
{
	struct rule_data_s *data = cookie;
	boolean_t match = B_FALSE;

	(void) bunyan_trace(data->rd_log, "Checking transform",
		    BUNYAN_T_STRING, "xftype", ikev2_xf_type_str(xf->xf_type),
		    BUNYAN_T_UINT32, "val", (uint32_t)xf->xf_id,
		    BUNYAN_T_END);

	switch (xf->xf_type) {
	case IKEV2_XF_ENCR:
		if (data->rd_xf->xf_encr != xf->xf_id)
			break;

		if (buflen > 0) {
			/*
			 * XXX: Verify if there should be attributes for this
			 * particular alg?
			 */
			data->rd_keylen_match = B_FALSE;
			VERIFY(ikev2_walk_xfattrs(buf, buflen,
			    match_rule_attr_cb, cookie, data->rd_log));

			/*
			 * RFC7296 3.3.6 - Unknown attribute means skip
			 * the transform, but not the whole proposal.
			 */
			if (data->rd_skip) {
				data->rd_skip = B_FALSE;
				break;
			}
			if (!data->rd_keylen_match)
				break;
		}
		data->rd_res->sar_encr = xf->xf_id;
		match = B_TRUE;
		break;
	case IKEV2_XF_AUTH:
		data->rd_has_auth = B_TRUE;
		if (data->rd_xf->xf_auth == xf->xf_id) {
			data->rd_res->sar_auth = xf->xf_id;
			match = B_TRUE;
		}
		break;
	case IKEV2_XF_PRF:
		if (xf->xf_id == data->rd_prf) {
			match = B_TRUE;
			data->rd_res->sar_prf = data->rd_prf;
		}
		break;
	case IKEV2_XF_DH:
		if (data->rd_xf->xf_dh == xf->xf_id) {
			match = B_TRUE;
			data->rd_res->sar_dh = xf->xf_id;
		}
		break;
	case IKEV2_XF_ESN:
		/* Not valid in IKE proposals */
		(void) bunyan_info(data->rd_log,
		    "Encountered ESN transform in IKE transform", BUNYAN_T_END);
		data->rd_skip = B_TRUE;
		break;
	default:
		/*
		 * RFC7296 3.3.6 - An unrecognized transform type means the
		 * proposal should be ignored.
		 */
		(void) bunyan_info(data->rd_log,
		    "Unknown transform type in proposal",
		    BUNYAN_T_UINT32, "xftype", (uint32_t)xf->xf_type,
		    BUNYAN_T_END);
		data->rd_skip = B_TRUE;
	}

	if (match) {
		(void) bunyan_trace(data->rd_log, "Partial match",
		    BUNYAN_T_STRING, "type", ikev2_xf_type_str(xf->xf_type),
		    BUNYAN_T_UINT32, "val", (uint32_t)xf->xf_id,
		    BUNYAN_T_END);
		data->rd_res->sar_match |= (uint32_t)1 << xf->xf_type;
	}

	return (!data->rd_skip);
}

static boolean_t
match_rule_attr_cb(ikev2_attribute_t *attr, void *cookie)
{
	struct rule_data_s *data = cookie;

	/* Only one attribute type is recognized currently */
	if (IKE_ATTR_GET_TYPE(attr->attr_type) != IKEV2_XF_ATTR_KEYLEN) {
		data->rd_skip = B_TRUE;
		return (B_FALSE);
	}

	if (attr->attr_length >= data->rd_xf->xf_minbits &&
	    attr->attr_length <= data->rd_xf->xf_maxbits) {
		data->rd_res->sar_encr_keylen = attr->attr_length;
		data->rd_keylen_match = B_TRUE;
		return (B_FALSE);
	}

	return (B_TRUE);
}

struct acquire_data_s {
	bunyan_logger_t		*ad_log;
	sadb_comb_t		*ad_comb;
	ikev2_sa_result_t	*ad_res;
	ikev2_spi_proto_t	ad_spitype;
	ikev2_dh_t		ad_dh;
	boolean_t		ad_skip;
	boolean_t		ad_match;
	boolean_t		ad_keylen_match;
};

static boolean_t match_acq_prop_cb(ikev2_sa_proposal_t *, uint64_t,
    uint8_t *, size_t, void *);
static boolean_t match_acq_xf_cb(ikev2_transform_t *, uint8_t *, size_t,
    void *);
static boolean_t match_acq_attr_cb(ikev2_attribute_t *, void *);

boolean_t
ikev2_sa_match_acquire(parsedmsg_t *restrict pmsg, ikev2_dh_t dh,
    pkt_t *restrict pkt, ikev2_sa_result_t *restrict result)
{
	pkt_payload_t *pay = pkt_get_payload(pkt, IKEV2_PAYLOAD_SA, NULL);
	bunyan_logger_t *l = pkt->pkt_sa->i2sa_log;
	sadb_msg_t *samsg = pmsg->pmsg_samsg;
	sadb_prop_t *prop;
	sadb_comb_t *comb;
	ikev2_spi_proto_t spitype = IKEV2_PROTO_NONE;

	VERIFY3P(pay, !=, NULL);

	(void) bunyan_debug(l, "Checking rules against acquire", BUNYAN_T_END);

	switch (samsg->sadb_msg_satype) {
	case SADB_SATYPE_AH:
		spitype = IKEV2_PROTO_AH;
		break;
	case SADB_SATYPE_ESP:
		spitype = IKEV2_PROTO_ESP;
		break;
	default:
		INVALID("sadb_msg_satype");
	}

	prop = (sadb_prop_t *)pmsg->pmsg_exts[SADB_EXT_PROPOSAL];
	comb = (sadb_comb_t *)(prop + 1);

	for (size_t i = 0; i < prop->sadb_x_prop_numecombs; i++, comb++) {
		struct acquire_data_s data = {
			.ad_log = l,
			.ad_comb = comb,
			.ad_res = result,
			.ad_spitype = spitype,
			.ad_dh = dh
		};

		(void) memset(result, 0, sizeof (*result));

		VERIFY(ikev2_walk_proposals(pay->pp_ptr, pay->pp_len,
		    match_acq_prop_cb, &data, l));

		if (data.ad_match) {
			(void) bunyan_debug(l, "Found proposal match",
			    BUNYAN_T_UINT32, "propnum",
			    (uint32_t)result->sar_propnum,
			    BUNYAN_T_UINT64, "spi", result->sar_spi,
			    BUNYAN_T_STRING, "encr",
			    ikev2_xf_encr_str(result->sar_encr),
			    BUNYAN_T_UINT32, "keylen",
			    (uint32_t)result->sar_encr_keylen,
			    BUNYAN_T_STRING, "auth",
			    ikev2_xf_auth_str(result->sar_auth),
			    BUNYAN_T_STRING, "prf",
			    ikev2_prf_str(result->sar_prf),
			    BUNYAN_T_STRING, "dh", ikev2_dh_str(result->sar_dh),
			    BUNYAN_T_UINT32, "esn", (uint32_t)result->sar_esn,
			    BUNYAN_T_END);
			return (B_TRUE);
		}
	}

	bunyan_debug(l, "No matching proposals found", BUNYAN_T_END);
	return (B_FALSE);
}

static boolean_t
match_acq_prop_cb(ikev2_sa_proposal_t *prop, uint64_t spi, uint8_t *buf,
    size_t buflen, void *cookie)
{
	struct acquire_data_s *data = cookie;

	if (prop->proto_protoid != data->ad_spitype) {
		bunyan_debug(data->ad_log, "Proposal is not for this SA type",
		    BUNYAN_T_STRING, "exp_satype",
		    ikev2_spi_str(data->ad_spitype),
		    BUNYAN_T_STRING, "prop_satype",
		    ikev2_spi_str(prop->proto_protoid),
		    BUNYAN_T_UINT32, "prop_satype_val",
		    (uint32_t)prop->proto_protoid, BUNYAN_T_END);
		return (B_FALSE);
	}

	(void) memset(data->ad_res, 0, sizeof (*data->ad_res));
	data->ad_skip = B_FALSE;

	VERIFY(ikev2_walk_xfs(buf, buflen, match_acq_xf_cb, cookie,
	    data->ad_log));

	if (data->ad_skip)
		return (B_TRUE);

	/*
	 * Go on to the next proposal if no match.  Check mandatory types
	 * and optional types if we've specified one.
	 * RFC7296 3.3.3 Lists mandatory and optional transform types
	 */
	switch (data->ad_spitype) {
	case IKEV2_PROTO_ESP:
		/* Mandatory: ENCR, ESN  Optional: AUTH, DH */
		if (!SA_RESULT_HAS(data->ad_res, IKEV2_XF_ENCR) ||
		    !SA_RESULT_HAS(data->ad_res, IKEV2_XF_ESN) ||
		    (data->ad_comb->sadb_comb_auth != SADB_AALG_NONE &&
		    !SA_RESULT_HAS(data->ad_res, IKEV2_XF_AUTH)) ||
		    (data->ad_dh != IKEV2_DH_NONE &&
		    !SA_RESULT_HAS(data->ad_res, IKEV2_XF_DH)))
			return (B_TRUE);
		break;
	case IKEV2_PROTO_AH:
		/* Mandatory: AUTH, ESN, Optional: DH */
		if (!SA_RESULT_HAS(data->ad_res, IKEV2_XF_AUTH) ||
		    !SA_RESULT_HAS(data->ad_res, IKEV2_XF_ESN) ||
		    (data->ad_dh != IKEV2_DH_NONE &&
		    !SA_RESULT_HAS(data->ad_res, IKEV2_XF_DH)))
			return (B_TRUE);
		break;
	case IKEV2_PROTO_NONE:
	case IKEV2_PROTO_IKE:
	case IKEV2_PROTO_FC_ESP_HEADER:
	case IKEV2_PROTO_FC_CT_AUTH:
		INVALID("data->ad_spitype");
		break;
	}

	return (B_FALSE);
}
static boolean_t
match_acq_xf_cb(ikev2_transform_t *xf, uint8_t *buf, size_t buflen,
    void *cookie)
{
	struct acquire_data_s *data = cookie;
	boolean_t match = B_FALSE;

	switch (xf->xf_type) {
	case IKEV2_XF_ENCR:
		if (xf->xf_id != data->ad_comb->sadb_comb_encrypt)
			break;
		/* XXX: match attr */
		break;
	case IKEV2_XF_PRF:
		bunyan_debug(data->ad_log,
		    "Encountered PRF transform in AH/ESP transform",
		    BUNYAN_T_END);
		data->ad_skip = B_TRUE;
		break;
	case IKEV2_XF_AUTH:
		if (xf->xf_id != data->ad_comb->sadb_comb_auth)
			break;
		match = B_TRUE;
		data->ad_res->sar_auth = xf->xf_id;
		break;
	case IKEV2_XF_DH:
		if (xf->xf_id != data->ad_dh)
			break;
		match = B_TRUE;
		data->ad_res->sar_dh = xf->xf_id;
		break;
	case IKEV2_XF_ESN:
		if (xf->xf_id != IKEV2_ESN_NONE)
			break;
		match = B_TRUE;
		data->ad_res->sar_esn = B_FALSE;
		break;
	}

	if (match)
		data->ad_res->sar_match |= (uint32_t)1 << xf->xf_type;

	return (!data->ad_skip);
}

static boolean_t
match_acq_attr_cb(ikev2_attribute_t *attr, void *cookie)
{
	struct acquire_data_s *data = cookie;

	if (attr->attr_type != IKEV2_XF_ATTR_KEYLEN) {
		data->ad_skip = B_TRUE;
		return (B_FALSE);
	}

	if (attr->attr_length >= data->ad_comb->sadb_comb_encrypt_minbits &&
	    attr->attr_length <= data->ad_comb->sadb_comb_encrypt_maxbits) {
		data->ad_res->sar_encr_keylen = attr->attr_length;
		data->ad_keylen_match = B_TRUE;
		return (B_FALSE);
	}

	return (B_TRUE);
}

void
ikev2_no_proposal_chosen(ikev2_sa_t *restrict i2sa, const pkt_t *restrict src,
    ikev2_spi_proto_t proto, uint64_t spi)
{
	pkt_t *resp = ikev2_pkt_new_response(src);

	if (resp == NULL)
		return;

	if (!ikev2_add_notify(resp, proto, spi, IKEV2_N_NO_PROPOSAL_CHOSEN,
	    NULL, 0)) {
		ikev2_pkt_free(resp);
		return;
	}

	/* Nothing can be done if send fails for this, so ignore return val */
	(void) ikev2_send(resp, B_TRUE);

	/* ikev2_send consumes packet, no need to free afterwards */
}

void
ikev2_invalid_ke(const pkt_t *src, ikev2_spi_proto_t proto, uint64_t spi,
    ikev2_dh_t dh)
{
	pkt_t *resp = ikev2_pkt_new_response(src);
	uint16_t val = htons((uint16_t)dh);

	if (resp == NULL)
		return;

	if (!ikev2_add_notify(resp, proto, spi, IKEV2_N_INVALID_KE_PAYLOAD,
	    &val, sizeof (val))) {
		ikev2_pkt_free(resp);
		return;
	}

	(void) ikev2_send(resp, B_TRUE);
}
