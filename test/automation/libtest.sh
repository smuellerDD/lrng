#!/bin/bash
#
# Execution helper library
#
# Copyright (C) 2021, Stephan Mueller <smueller@chronox.de>
#
# License: see LICENSE file in root directory
#
# THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
# WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
# OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE, ALL OF
# WHICH ARE HEREBY DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
# OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
# BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
# LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
# USE OF THIS SOFTWARE, EVEN IF NOT ADVISED OF THE POSSIBILITY OF SUCH
# DAMAGE.
#

###################################################################
# Test configuration - may be changed
###################################################################

# The KERNEL_BASE points to the directory where the Linux kernel sources
# are located. These kernel sources must be properly configured as
# documented in https://github.com/vincentbernat/eudyptula-boot
#
# If needed, the kernel is automatically compiled and installed
# for use by eudyptula-boot.
#
# This directory will also hold the directory for the kernel modules of the
# kernel. If you recompile modules, delete the respective binary directory
# to cause the test framework to populate it again from the kernel source
# dir.
#
# Note, this must be an absolute path name
KERNEL_BASE="/home/sm/hacking/testing"

# The TESTKERN points to the subdirectory found in KERNEL_BASE that holds
# the actual compiled binary. For example, you unpack the linux-5.12.tar.xz
# in the KERNEL_BASE directory. Then you set TESTKERN to "linux-5.12"
TESTKERN="linux-5.18.1"

# Directory relative to your user's $HOME that will be used as a temporary
# file directory for the test harness. This directory will also hold
# a log file that shows the entire test run.
TMPDIR="lrng_testing"

###################################################################
# Code - do not change
###################################################################

in_hypervisor()
{
	if (dmesg | grep -q "Hypervisor detected: KVM")
	then
		return 1
	else
		return 0
	fi
}

CURR_DIR=$PWD

HOMEDIR_REAL="${HOME}/${TMPDIR}"
HOMEDIR_VIRT="/root/${TMPDIR}"
$(in_hypervisor)
if [ $? -eq 0 ]
then
	HOMEDIR=$HOMEDIR_REAL
else
	HOMEDIR=$HOMEDIR_VIRT
fi

TESTCASEFILE="${HOMEDIR}/testcase"
FAILUREFILE="${HOMEDIR}/failurecount"
FAILUREFILE_GLOBAL="${HOMEDIR}/failurecount_global"
LOGFILE="${HOMEDIR}/logfile"

EUDYPTULA=$(type -p "eudyptula-boot")

color()
{
	bg=0
	echo -ne "\033[0m"
	while [[ $# -gt 0 ]]; do
		code=0
		case $1 in
			black) code=30 ;;
			red) code=31 ;;
			green) code=32 ;;
			yellow) code=33 ;;
			blue) code=34 ;;
			magenta) code=35 ;;
			cyan) code=36 ;;
			white) code=37 ;;
			background|bg) bg=10 ;;
			foreground|fg) bg=0 ;;
			reset|off|default) code=0 ;;
			bold|bright) code=1 ;;
		esac
		[[ $code == 0 ]] || echo -ne "\033[$(printf "%02d" $((code+bg)))m"
		shift
	done
}

echo_pass()
{
 	echo $(color "green")[PASSED]$(color off) $@ >> $LOGFILE
 	echo $(color "green")[PASSED]$(color off) $@ >&2
 	sync
}

get_failures_common()
{
	local file=$1
	if [ -f $file ]
	then
		cat $file
	else
		echo 0
	fi
}

get_failures()
{
	echo $(get_failures_common $FAILUREFILE)
}

get_failures_global()
{
	echo $(get_failures_common $FAILUREFILE_GLOBAL)
}

echo_fail()
{
	echo $(color "red")[FAILED]$(color off) $@ >&2
	echo $(color "red")[FAILED]$(color off) $@ >> $LOGFILE
	local failures=$(get_failures)
	failures=$(($failures+1))
	echo $failures > $FAILUREFILE

	failures=$(get_failures_global)
	failures=$(($failures+1))
	echo $failures > $FAILUREFILE_GLOBAL
	sync

}

echo_deact()
{
	echo $(color "yellow")[DEACTIVATED]$(color off) $@ >&2
	echo $(color "yellow")[DEACTIVATED]$(color off) $@ >> $LOGFILE
	sync
}

echo_log()
{
	echo $@ >> $LOGFILE
	echo $@ >&2
	sync
}

echo_final()
{
	local failures=$(get_failures_global)
	echo "=========================================================="
	echo "===================== Final Verdict ======================"
	echo "=========================================================="
	if [ $failures -eq 0 ]
	then
		echo_pass "ALL TESTS PASSED"
	else
		echo_fail "TEST FAILURES: $failures"
	fi
}

exit_test()
{
	trap "" 0 1 2 3 15
	exit $failures
}

showstatus()
{
	$(in_hypervisor)
	if [ $? -eq 1 ]
	then
		return
	fi

	local failures=$(get_failures)
	echo "=========================================================="
	if [ $failures -gt 0 ]
	then
		echo $(color "red")[FAILED]$(color off) $@ "$local_failures failures for module $0"
	else
		echo_pass "no failures for module $0"
	fi
}

cleanup()
{
	$(in_hypervisor)
	if [ $? -eq 1 ]
	then
		return
	fi

	echo "========= Testing ended $(date) ================ " >> $LOGFILE

	rm -f $FAILUREFILE
	rm -f $TESTCASEFILE
}

check_kernel_config()
{
	local kernel_ver=$TESTKERN
	local kernel_src="${KERNEL_BASE}/${kernel_ver}"
	local option=$1

	if [ ! -f "$kernel_src/.config" ]
	then
		echo_deact "Kernel configuration not found in $kernel_src/.config"
		return 0
	fi

	if (grep -q "$option" "$kernel_src/.config")
	then
		return 0
	else
		echo_deact "Kernel configuration option not as expected: $option"
		return 1
	fi
}

execvirt()
{
	local script=$1
	shift

	local kernel_ver=$TESTKERN
	local depmod=0
	local kernel_src="${KERNEL_BASE}/${kernel_ver}"
	local kernel_build="${KERNEL_BASE}/build/${kernel_ver}"
	local kernel_binary=""
	local moddir=""

	echo "Testing Linux kernel version $kernel_ver in directory $KERNEL_BASE"

	if [ ! -d "$kernel_src" ]
	then
		echo "No kernel source directory found"
		exit 1
	fi
	cd $kernel_src

	if [ ! -f .config ]
	then
		echo "No configured kernel found in $(pwd)"
		exit 1
	fi
	if ! grep -q "CONFIG_9P_FS=y" .config
	then
		echo "No virtme compliant kernel config found"
		exit 1
	fi

	# Build and install fully configured kernel
	if [ ! -d ${kernel_build} ]
	then
		make -j2
		make modules_install install INSTALL_MOD_PATH=${kernel_build} INSTALL_PATH=${kernel_build}
		depmod=1
	fi

	# get latest modules directory
	moddir=${kernel_build}/lib/modules
	local version=$(ls -t ${moddir} | head -n1)
	moddir=${moddir}/$version
	if [ ! -d "$moddir" ]
	then
		echo "No directory $moddir"
		exit 1
	fi

	kernel_binary=${kernel_build}/vmlinuz-$version
	if [ ! -f $kernel_binary ]
	then
		echo "Kernel binary $kernel_binary not found"
		exit 1
	fi

	if [ $depmod -ne 0 ]
	then
		depmod -b ${kernel_build} $version
	fi

	echo_log "Executing test with kernel command line $@"
	echo_log "Executing test case $script"

	$EUDYPTULA -m 2G "-c \\\"dyndbg=file drivers/char/lrng/* +p\\\" $@" --kernel $kernel_binary $script
	if [ $? -ne 0 ]
	then
		local ret=$?
		echo_fail "Test for kernel version $kernel_ver failed"
		exit $ret
	fi

	cd $CURR_DIR
}

write_cmd()
{
	echo $@ > $TESTCASEFILE
}

read_cmd()
{
	local cmd=$(cat $TESTCASEFILE)

	echo_log $(color "blue")[INFO]$(color off) "Executing test $cmd"
	echo $cmd
}

full_scriptname()
{
	if [ $(echo $1 | sed 's/^\(.\).*$/\1/') = "/" ]
	then
		echo $1
	else
		echo $PWD/$1
	fi
}

init()
{
	$(in_hypervisor)
	if [ $? -eq 1 ]
	then
		return
	fi

	mkdir -p ${HOMEDIR}
	if [ $? -ne 0 ]
	then
		echo "cannot create home directory $HOMEDIR"
		exit 1
	fi

	echo "========= Testing started $(date) ================ " >> $LOGFILE
}

trap "showstatus; cleanup; exit" 0 1 2 3 15

if [ ! -d "$KERNEL_BASE" -a -h "./kernel-sources" ]
then
	echo_deact "Linux kernel sources directory not found - skipping eudyptula-boot tests"
	exit 0
fi


if [ ! -x $EUDYPTULA ]
then
	echo_deact "$EUDYPTULA not found"
	exit 0
fi

init

check_kernel_config "CONFIG_LRNG=y" || exit 1
check_kernel_config "CONFIG_DYNAMIC_DEBUG=y" || exit 1
