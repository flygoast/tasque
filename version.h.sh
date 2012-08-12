#! /bin/bash
LC_ALL=C
LANG=C
rm -f version.h
LOCALVER=`git rev-list HEAD | wc -l| awk '{print \$1}'`
if [ $LOCALVER -gt 1 ]; then
    VER=`git rev-list origin/master | wc -l | awk '{print \$1}'`
    if [ $VER -ne $LOCALVER ]; then
        VER="$VER+$(($LOCALVER-$VER))"
    fi

    if git status | grep -q "modified:"; then
        VER="$VER"M
    fi
    VER="$VER $(git rev-list HEAD -n 1 | cut -c 1-7)"
else
    VER="x"
fi

TAG=`git tag | head -n1`
FULL_VERSION="$TAG.$VER"
cat version.h.template | sed "s/\$GIT_VERSION/$FULL_VERSION/g" > version.h
echo "Generate version.h"
