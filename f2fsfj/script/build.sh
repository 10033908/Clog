make_command=$1

function make_vfs_ko(){
    make $MODULE_NAME LLVM=y
    if [ $? -eq 0 ]; then
        echo -e "\e[32mko file compile success\e[0m"
        cp ./build/lib/modules/5.15.39/extra/f2fsfj.ko ../f2fsfj_build/f2fsfj.ko
        cp ./build/lib/modules/5.15.39/extra/f2fsfj.ko /home/ytcui22/f2fs-fj/filebench/script/f2fsfj.ko
        cp ./build/lib/modules/5.15.39/extra/f2fsfj.ko ../crash_test/f2fsfj.ko
        sudo cp ./build/lib/modules/5.15.39/extra/f2fsfj.ko /lib/modules/5.15.39/f2fsfj.ko
    return 0

    else
        echo "make f2fsfj.ko fail"
        return 1
    fi
}

function make_clean(){
    make clean
    if [ $? -eq 0 ]; then
        echo -e "\e[32mmake clean over \e[0m"
        return 0
    else
        echo "make clean happen exception"
        return 0
    fi
}

##############################
function mount_fs(){
    sudo insmod /lib/modules/5.15.39/f2fsfj.ko
    if [ $? -eq 0 ]; then
        echo -e "\e[32m insmod f2fsfj.ko success\e[0m"
    else
        echo "insmod f2fsfj.ko fail"
        return 1
    fi

    sudo mount -t f2fsfj $dev_path $mount_path
    #sudo mount -t f2fs -o noatime -o nodiratime $dev_path $mount_path
    if [ $? -eq 0 ]; then
        echo -e "\e[32m mount f2fs in $mount_path\e[0m"
        mount | grep f2fs_mount
        return 0
    else
        echo "mount f2fs in $mount_path fail"
        return 1
    fi
}

function umount_fs(){
    sudo umount $dev_path
    if [ $? -eq 0 ]; then
        echo -e "\e[32m umount f2fs success\e[0m"
        mount | grep f2fs_mount
        sudo rmmod f2fsfj
        if [ $? -eq 0 ]; then
            echo -e "\e[32m rmmod f2fsfj in $mount_path\e[0m"
            return 0
        else
            echo "rmmod f2fsfj in $mount_path fail"
            return 1
        fi
    else
        echo "umount f2fs fail"
        return 1
    fi
}


##############################
if [ $make_command == "f2fsfj" ]; then
    make_vfs_ko
    if [ $? -ne 0 ]; then
        exit
    fi
    make_clean
    if [ $? -ne 0 ]; then
        exit
    fi
    echo -e "\e[32m***************************************\e[0m"
    echo -e "\e[32m*       make f2fsfj succuss            *\e[0m"
    echo -e "\e[32m*       find in ../../f2fsfj_build     *\e[0m"
    echo -e "\e[32m***************************************\e[0m"
    echo -e $(date)
elif [ $make_command == "-h" ]; then
    echo "./script/build.sh f2fsfj, make f2fsfj.ko"
fi
