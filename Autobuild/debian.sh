BUILD_DEPS=`awk 'BEGIN {cnt = 1;} /^Build-Depends:/ {split($0, line, ":");split(line[2], deps, ",");for (i in deps) {d = deps[i];sub(/^ */, "", d);sub(/ *$/, "", d);split(d, tokens, " ");packages[cnt] = tokens[1];cnt++;}} END {out = ""; for(i = 1; i <= cnt; i++) {out = out packages[i] " ";} print out; }' debian/control`
CHANGELOG=debian/changelog
NOW=`date -R`
VER=`git describe | sed "s/\([0-9]*\)\.\([0-9]*\)-\([0-9]*\)-.*/\1.\2.\3/"`

build() 
{
    echo >${CHANGELOG} "showtime (${VER}) unstable; urgency=low"
    echo >>${CHANGELOG}
    echo >>${CHANGELOG} "  * The full changelog can be found at "
    echo >>${CHANGELOG} "    http://www.lonelycoder.com/showtime/download"
    echo >>${CHANGELOG}
    echo >>${CHANGELOG} " -- Andreas Öman <andreas@lonelycoder.com>  ${NOW}"
    
    export JOBSARGS
    export JARGS
    export ARCH

    which ccache >/dev/null
    if [ $? -eq 0 ]; then
	echo "Using ccache"
	ccache -s
	USE_CCACHE="--ccache"
    else
	USE_CCACHE=""
    fi
    export USE_CCACHE

    dpkg-buildpackage -b -us -uc

    for a in ../showtime*${VER}*.deb; do
	versioned_artifact "$a" deb application/x-deb `basename $a`
    done

    for a in ../showtime*${VER}*.changes; do
	versioned_artifact "$a" changes text/plain `basename $a`
    done
}

clean() 
{
    for a in ../showtime*${VER}*.deb; do
	rm -f "$a"
    done

    for a in ../showtime*${VER}*.changes; do
	rm -f "$a"
    done

    rm -f ${CHANGELOG}
    dh_clean
}

deps() 
{
    if [[ $EUID -ne 0 ]]; then
	echo "Build dependencies must be installed as root"
	exit 1
    fi
    apt-get -y install ${BUILD_DEPS}
}

buildenv() 
{
    echo $BUILD_DEPS | shasum | awk '{print $1}'
}

eval $OP
