#!/bin/bash
# Mock benchmark for testing Pilot CLI. This script displays one mock benchmark
# result on each run. Progress is stored at /tmp/pilot_mock_benchmark_round.txt.
#
# Copyright (c) 2016 University of California, Santa Cruz, CA, USA.
# Created by Yan Li <yanli@ascar.io>,
# Department of Computer Science, Baskin School of Engineering.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in the
#       documentation and/or other materials provided with the distribution.
#     * Neither the name of the Storage Systems Research Center, the
#       University of California, nor the names of its contributors
#       may be used to endorse or promote products derived from this
#       software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
# FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
# REGENTS OF THE UNIVERSITY OF CALIFORNIA BE LIABLE FOR ANY DIRECT,
# INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
# STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
# OF THE POSSIBILITY OF SUCH DAMAGE.
set -e -u

# These sample response time are taken from [Ferrari78], page 79.
DATA=(1.21 1.67 1.71 1.53 2.03 2.15 1.88 2.02 1.75 1.84 1.61 1.35 1.43 1.64 1.52 1.44 1.17 1.42 1.64 1.86 1.68 1.91 1.73 2.18 2.27 1.93 2.19 2.04 1.92 1.97 1.65 1.71 1.89 1.70 1.62 1.48 1.55 1.39 1.45 1.67 1.62 1.77 1.88 1.82 1.93 2.09 2.24 2.16)

ROUND_FILE=/tmp/pilot_mock_benchmark_round.txt

if [ -f $ROUND_FILE ]; then
    ROUND=`cat $ROUND_FILE`
else
    ROUND=0
fi
if [ $ROUND -ge ${#DATA[@]} ]; then
    rm "$ROUND_FILE"
    exit 1
fi
echo ${DATA[$ROUND]}

ROUND=`expr $ROUND + 1`
echo $ROUND >$ROUND_FILE
