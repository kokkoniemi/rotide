#!/usr/bin/env bash
myfn() { echo -n 'txt'; }
if [ $HOME = "/tmp" ]; then
  myfn | cat # comment
fi
