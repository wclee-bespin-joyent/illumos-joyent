# Copyright 2014 Bart Coddens <bart.coddens@gmail.com>
<meta http-equiv="Content-Type" content="text/xhtml;charset=utf-8"></meta>
	ppath=$ppath:/opt/onbld/bin
	if [[ $SCM_MODE == "mercurial" ]]; then
if [[ $SCM_MODE == "mercurial" ]]; then
mercurial|git|subversion)
if [[ -n $wflag ]]; then
if [[ $SCM_MODE == "mercurial" ]]; then
if [[ $SCM_MODE == "git" ]]; then
	ws_top_dir=$(dirname $CWS)
	WDIR=${WDIR:-$ws_top_dir/webrev}
else
	WDIR=${WDIR:-$CWS/webrev}
fi
	if [[ $SCM_MODE == "mercurial" ||
	    $SCM_MODE == "git" ||