#!/bin/bash
# Script outline to install and build kernel.
# Author: Siddhant Jajoo.

set -e
set -u

OUTDIR=/tmp/aeld
KERNEL_REPO=git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.15.163
BUSYBOX_VERSION=1_33_1
FINDER_APP_DIR=$(realpath $(dirname $0))
ARCH=arm64
CROSS_COMPILE=aarch64-none-linux-gnu-
TOOLCHAIN_SYSROOT=/opt/arm/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-linux-gnu/aarch64-none-linux-gnu/libc

if [ $# -lt 1 ]
then
	echo "Using default directory ${OUTDIR} for output"
else
	OUTDIR=$1
	echo "Using passed directory ${OUTDIR} for output"
fi

mkdir -p ${OUTDIR}

if [ $? -ne 0 ]; then
	# exit with failure if directory could not be created
	exit -1
fi

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/linux-stable" ]; then
    #Clone only if the repository does not exist.
	echo "CLONING GIT LINUX STABLE VERSION ${KERNEL_VERSION} IN ${OUTDIR}"
	git clone ${KERNEL_REPO} --depth 1 --single-branch --branch ${KERNEL_VERSION}
fi
if [ ! -e ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ]; then
    cd linux-stable
    echo "Checking out version ${KERNEL_VERSION}"
    git checkout ${KERNEL_VERSION}

    # TODO: Add your kernel build steps here
	echo "make mrproper..."
	make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} mrproper
	echo "make defconfig..."
	make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} defconfig
	echo "make all..."
	make -j4 ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} all
	echo "make modules..."
	make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} modules
	echo "make dtbs..."
	make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} dtbs
fi

echo "Adding the Image in outdir"
cp ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ${OUTDIR}/Image

echo "Creating the staging directory for the root filesystem"
cd "$OUTDIR"
if [ -d "${OUTDIR}/rootfs" ]
then
	echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
    sudo rm  -rf ${OUTDIR}/rootfs
fi

# TODO: Create necessary base directories
mkdir rootfs
cd rootfs
mkdir -p bin dev etc home lib lib64 proc sbin sys tmp usr var
mkdir -p usr/bin usr/lib usr/sbin
mkdir -p var/log
mkdir -p home/conf

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/busybox" ]
then
	git clone git://busybox.net/busybox.git
    cd busybox
    git checkout ${BUSYBOX_VERSION}
    # TODO:  Configure busybox
	make distclean
	make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} defconfig
	make -j4 ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} all
else
    cd busybox
fi

# TODO: Make and install busybox
make CONFIG_PREFIX=${OUTDIR}/rootfs ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} install
chmod u+s ${OUTDIR}/rootfs/bin/busybox
cd ${OUTDIR}/rootfs

echo "Library dependencies"
${CROSS_COMPILE}readelf -a ${OUTDIR}/rootfs/bin/busybox | grep "program interpreter"
${CROSS_COMPILE}readelf -a ${OUTDIR}/rootfs/bin/busybox | grep "Shared library"

# TODO: Add library dependencies to rootfs
# www.gnu.org/software/sed/manual/sed.html#sed-regular-expressions to figure out how to extract a string from output of previous command
# Asked ChatGPT on how to approach the multi-line output of the shared libraries line
INTERPRETER_PATH=$(${CROSS_COMPILE}readelf -a ${OUTDIR}/rootfs/bin/busybox | grep "program interpreter" | sed -E 's/\[.*: \/(.*)\]/\1/')
mkdir -p "$(dirname $INTERPRETER_PATH)"
INTERPRETER_PATH=$(echo $INTERPRETER_PATH | tr -d "[:space:]")
FULL_INTERPRETER_PATH="${TOOLCHAIN_SYSROOT}/${INTERPRETER_PATH}"
cp $FULL_INTERPRETER_PATH ${OUTDIR}/rootfs/lib

SHARED_LIBS=$(${CROSS_COMPILE}readelf -a ${OUTDIR}/rootfs/bin/busybox | grep "Shared library" | sed -E 's/.*\[(.*)]/\1/')
for CURR_LIB in $SHARED_LIBS; do
	echo $CURR_LIB
	cp ${TOOLCHAIN_SYSROOT}/lib64/${CURR_LIB} ${OUTDIR}/rootfs/lib64
done

# TODO: Make device nodes
sudo mknod -m 666 ${OUTDIR}/rootfs/dev/null c 1 3
sudo mknod -m 666 ${OUTDIR}/rootfs/dev/console c 5 1
sudo mknod -m 666 ${OUTDIR}/rootfs/dev/tty c 5 0

# TODO: Clean and build the writer utility
cd ${FINDER_APP_DIR}
make clean
make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE}  writer
cp writer ${OUTDIR}/rootfs/home/

# TODO: Copy the finder related scripts and executables to the /home directory
# on the target rootfs
cp ${FINDER_APP_DIR}/finder.sh ${OUTDIR}/rootfs/home/
cp ${FINDER_APP_DIR}/finder-test.sh ${OUTDIR}/rootfs/home/
cp ${FINDER_APP_DIR}/autorun-qemu.sh ${OUTDIR}/rootfs/home/
cp ${FINDER_APP_DIR}/../conf/assignment.txt ${OUTDIR}/rootfs/home/conf/
cp ${FINDER_APP_DIR}/../conf/username.txt ${OUTDIR}/rootfs/home/conf/

# TODO: Chown the root directory
sudo chown -R root:root ${OUTDIR}/rootfs/
sudo chmod u+s ${OUTDIR}/rootfs/bin/busybox

# TODO: Create initramfs.cpio.gz
# Received assistance from Dan; needed to cd to the rootfs and then use find, or the /tmp/ path would be included in the rootfs
# this would cause a reference to the /tmp at the rootfs when starting qemu, which would mean the other directories (bin, sbin, etc.)
# don't exist.
cd "${OUTDIR}/rootfs"
find . | cpio -H newc -ov --owner root:root > ${OUTDIR}/initramfs.cpio
gzip -f ${OUTDIR}/initramfs.cpio
