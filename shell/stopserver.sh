#/bin/sh
kill -9 $( ps -e|grep wfs |awk '{print $1}')
