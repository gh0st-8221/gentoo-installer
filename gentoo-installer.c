#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void execute_cmd_abort(const char *cmd, const char *err_msg) {
    if (system(cmd) != 0) {
        printf("\nFATAL ERROR: %s\nCommand failed: %s\nExiting installation.\n", err_msg, cmd);
        exit(1);
    }
}

void execute_cmd(const char *cmd) {
    system(cmd);
}

void get_string(const char *prompt, char *out, size_t size) {
    printf("%s", prompt);
    char format[16];
    snprintf(format, sizeof(format), " %%%zu[^\n]", size - 1);
    scanf(format, out);
}

int check_internet(void) {
    return system("ping -c 1 8.8.8.8 > /dev/null 2>&1") == 0;
}

void setup_wifi(void) {
    char ssid[128];
    char pass[128];
    char cmd[512];

    printf("Scanning for Wi-Fi networks...\n");
    execute_cmd("nmcli dev wifi rescan > /dev/null 2>&1");
    sleep(3);
    execute_cmd("nmcli dev wifi list");

    get_string("\nEnter SSID: ", ssid, sizeof(ssid));
    get_string("Enter Wi-Fi Password: ", pass, sizeof(pass));

    snprintf(cmd, sizeof(cmd), "nmcli dev wifi connect '%s' password '%s'", ssid, pass);
    execute_cmd(cmd);
}

void ensure_network(void) {
    if (check_internet()) {
        printf("Internet connection detected.\n");
        return;
    }

    printf("No internet connection found.\n");
    char choice[8];
    get_string("Do you want to configure Wi-Fi? (y/n): ", choice, sizeof(choice));

    if (choice[0] == 'y' || choice[0] == 'Y') {
        setup_wifi();
        if (!check_internet()) {
            printf("Failed to connect to internet. Exiting...\n");
            exit(1);
        }
    } else {
        printf("Internet required for Gentoo installation. Exiting...\n");
        exit(1);
    }
}

int check_uefi(void) {
    return access("/sys/firmware/efi", F_OK) == 0;
}

void select_disk(char *disk_out, size_t size) {
    printf("\nAvailable Disks:\n");
    execute_cmd("lsblk -d -n -o NAME,SIZE,MODEL | grep -v loop");

    char input[64];
    get_string("\nEnter disk name to install to (e.g. sda or nvme0n1): ", input, sizeof(input));

    if (strncmp(input, "/dev/", 5) == 0) {
        snprintf(disk_out, size, "%s", input);
    } else {
        snprintf(disk_out, size, "/dev/%s", input);
    }
}

void get_stage3_url(char *stage3_url_out, size_t size) {
    printf("Fetching dynamic Stage3 URL from Gentoo mirrors...\n");
    execute_cmd_abort("wget -q https://distfiles.gentoo.org/releases/amd64/autobuilds/latest-stage3-amd64-openrc.txt -O /tmp/latest.txt", "Failed to download latest.txt");

    FILE *f = fopen("/tmp/latest.txt", "r");
    char line[256] = {0};
    char rel_path[256] = {0};

    if (f) {
        while (fgets(line, sizeof(line), f)) {
            if (line[0] != '#' && line[0] != '\n' && line[0] != '\r' && strlen(line) > 5) {
                sscanf(line, "%255s", rel_path);
                break;
            }
        }
        fclose(f);
    }

    if (strlen(rel_path) > 0) {
        snprintf(stage3_url_out, size, "https://distfiles.gentoo.org/releases/amd64/autobuilds/%s", rel_path);
        printf("Found latest Stage3: %s\n", stage3_url_out);
    } else {
        printf("Failed to parse Stage3 URL. Exiting...\n");
        exit(1);
    }
}

void partition_disk(const char *disk, char *p1, char *p2, size_t p_size) {
    char cmd[512];

    if (strstr(disk, "nvme") != NULL || strstr(disk, "mmcblk") != NULL || strstr(disk, "loop") != NULL) {
        snprintf(p1, p_size, "%sp1", disk);
        snprintf(p2, p_size, "%sp2", disk);
    } else {
        snprintf(p1, p_size, "%s1", disk);
        snprintf(p2, p_size, "%s2", disk);
    }

    printf("Cleaning up disk targets...\n");
    execute_cmd("swapoff -a 2>/dev/null");
    snprintf(cmd, sizeof(cmd), "umount -f %s* 2>/dev/null", disk);
    execute_cmd(cmd);
    
    snprintf(cmd, sizeof(cmd), "wipefs -a %s", disk);
    execute_cmd_abort(cmd, "Failed to wipe existing filesystem signatures");

    printf("Partitioning disk %s...\n", disk);
    snprintf(cmd, sizeof(cmd), "parted -s %s mklabel gpt", disk);
    execute_cmd_abort(cmd, "Failed to create GPT label");

    snprintf(cmd, sizeof(cmd), "parted -s %s mkpart ESP fat32 1MiB 1025MiB", disk);
    execute_cmd_abort(cmd, "Failed to create EFI partition");

    snprintf(cmd, sizeof(cmd), "parted -s %s set 1 boot on", disk);
    execute_cmd_abort(cmd, "Failed to set boot flag");

    snprintf(cmd, sizeof(cmd), "parted -s %s mkpart root ext4 1025MiB 100%%", disk);
    execute_cmd_abort(cmd, "Failed to create Root partition");

    snprintf(cmd, sizeof(cmd), "partprobe %s", disk);
    execute_cmd(cmd);
    execute_cmd("udevadm settle");
    sleep(2);

    printf("Formatting partitions...\n");
    snprintf(cmd, sizeof(cmd), "mkfs.vfat -F32 %s", p1);
    execute_cmd_abort(cmd, "Failed to format EFI partition");

    snprintf(cmd, sizeof(cmd), "mkfs.ext4 -F %s", p2);
    execute_cmd_abort(cmd, "Failed to format Root partition");
}

int main(void) {
    char disk[128];
    char p1[128], p2[128];
    char hostname[64];
    char root_pass[128], username[64], user_pass[128];
char stage3_url[512], cmd[1024];

    ensure_network();

    int is_uefi = check_uefi();
    printf("Boot Mode: %s\n", is_uefi ? "UEFI" : "Legacy BIOS");

    select_disk(disk, sizeof(disk));

    get_string("Enter Hostname: ", hostname, sizeof(hostname));
    get_string("Enter Root Password: ", root_pass, sizeof(root_pass));
    get_string("Enter Username: ", username, sizeof(username));
    get_string("Enter User Password: ", user_pass, sizeof(user_pass));

    get_stage3_url(stage3_url, sizeof(stage3_url));

    printf("\n=======================================================\n");
    printf("All parameters collected successfully!\n");
    printf("Starting fully automated installation. Do not interrupt.\n");
    printf("=======================================================\n\n");

    partition_disk(disk, p1, p2, sizeof(p2));

    printf("Mounting partitions...\n");
    execute_cmd("mkdir -p /mnt/gentoo");
    snprintf(cmd, sizeof(cmd), "mount %s /mnt/gentoo", p2);
    execute_cmd_abort(cmd, "Failed to mount Root partition");

    execute_cmd("mkdir -p /mnt/gentoo/efi");
    if (is_uefi) {
        snprintf(cmd, sizeof(cmd), "mount %s /mnt/gentoo/efi", p1);
        execute_cmd_abort(cmd, "Failed to mount EFI partition");
    }

    printf("Downloading Stage3 Tarball...\n");
    snprintf(cmd, sizeof(cmd), "wget %s -O /mnt/gentoo/stage3.tar.xz", stage3_url);
    execute_cmd_abort(cmd, "Failed to download Stage3 tarball");

    printf("Unpacking Stage3...\n");
    execute_cmd_abort("tar xpvf /mnt/gentoo/stage3.tar.xz --xattrs-include='*.*' --numeric-owner -C /mnt/gentoo", "Failed to extract Stage3");

    printf("Configuring make.conf...\n");
    FILE *make_conf = fopen("/mnt/gentoo/etc/portage/make.conf", "a");
    if (make_conf) {
        fprintf(make_conf, "\nCOMMON_FLAGS=\"-O2 -pipe -march=native\"\n");
        fprintf(make_conf, "CFLAGS=\"${COMMON_FLAGS}\"\n");
        fprintf(make_conf, "CXXFLAGS=\"${COMMON_FLAGS}\"\n");
        fprintf(make_conf, "FCFLAGS=\"${COMMON_FLAGS}\"\n");
        fprintf(make_conf, "FFLAGS=\"${COMMON_FLAGS}\"\n");
        fprintf(make_conf, "MAKEOPTS=\"-j%ld\"\n", sysconf(_SC_NPROCESSORS_ONLN));
        fprintf(make_conf, "ACCEPT_LICENSE=\"*\"\n");
        fprintf(make_conf, "FEATURES=\"getbinpkg\"\n");
        fprintf(make_conf, "EMERGE_DEFAULT_OPTS=\"--getbinpkg=y --binpkg-respect-use=y\"\n");
        fprintf(make_conf, "EDITOR=\"hx\"\n");
        if (is_uefi) {
            fprintf(make_conf, "GRUB_PLATFORMS=\"efi-64\"\n");
        }
        fclose(make_conf);
    }

    printf("Copying DNS configuration...\n");
    execute_cmd_abort("cp --dereference /etc/resolv.conf /mnt/gentoo/etc/", "Failed to copy DNS info");

    printf("Mounting virtual filesystems...\n");
    execute_cmd_abort("mount --types proc /proc /mnt/gentoo/proc", "Failed to mount proc");
    execute_cmd_abort("mount --rbind /sys /mnt/gentoo/sys && mount --make-rslave /mnt/gentoo/sys", "Failed to mount sys");
    execute_cmd_abort("mount --rbind /dev /mnt/gentoo/dev && mount --make-rslave /mnt/gentoo/dev", "Failed to mount dev");
    execute_cmd_abort("mount --bind /run /mnt/gentoo/run && mount --make-slave /mnt/gentoo/run", "Failed to mount run");

    FILE *script = fopen("/mnt/gentoo/setup_chroot.sh", "w");
    if (!script) {
        printf("FATAL ERROR: Failed to create chroot script.\n");
        exit(1);
    }

    fprintf(script, "#!/bin/bash\nset -e\nsource /etc/profile\nexport PS1=\"(chroot) ${PS1}\"\n\n");

    fprintf(script, "ROOT_UUID=$(blkid -s UUID -o value %s)\n", p2);
    fprintf(script, "echo \"UUID=${ROOT_UUID} / ext4 defaults,noatime 0 1\" > /etc/fstab\n");
    
    if (is_uefi) {
        fprintf(script, "EFI_UUID=$(blkid -s UUID -o value %s)\n", p1);
        fprintf(script, "echo \"UUID=${EFI_UUID} /efi vfat defaults,noatime 0 2\" >> /etc/fstab\n");
    }

    fprintf(script, "echo 'Europe/Kyiv' > /etc/timezone\n");
    fprintf(script, "ln -sf /usr/share/zoneinfo/Europe/Kyiv /etc/localtime\n");

    fprintf(script, "echo 'en_US.UTF-8 UTF-8' >> /etc/locale.gen\n");
    fprintf(script, "locale-gen\n");
    fprintf(script, "echo 'LANG=\"en_US.UTF-8\"' > /etc/env.d/02locale\n");
    fprintf(script, "echo 'LC_COLLATE=\"C.UTF-8\"' >> /etc/env.d/02locale\n");
    fprintf(script, "env-update && source /etc/profile\n");

    fprintf(script, "emerge-webrsync\n");

    if (is_uefi) {
        fprintf(script, "emerge --quiet app-editors/helix sys-kernel/gentoo-kernel-bin sys-kernel/linux-firmware sys-boot/grub sys-boot/efibootmgr net-misc/networkmanager sys-fs/dosfstools\n");
    } else {
        fprintf(script, "emerge --quiet app-editors/helix sys-kernel/gentoo-kernel-bin sys-kernel/linux-firmware sys-boot/grub net-misc/networkmanager sys-fs/dosfstools\n");
    }

    fprintf(script, "echo '%s' > /etc/hostname\n", hostname);

    fprintf(script, "useradd -m -G wheel,portage,audio,video,usb,cdrom -s /bin/bash %s\n", username);
    
    fprintf(script, "chpasswd << 'EOF'\n");
    fprintf(script, "root:%s\n", root_pass);
    fprintf(script, "%s:%s\n", username, user_pass);
    fprintf(script, "EOF\n");

    if (is_uefi) {
        fprintf(script, "grub-install --target=x86_64-efi --efi-directory=/efi --removable\n");
    } else {
        fprintf(script, "grub-install %s\n", disk);
    }
    fprintf(script, "grub-mkconfig -o /boot/grub/grub.cfg\n");

    fprintf(script, "rc-update add NetworkManager default\n");
    
    fclose(script);

    execute_cmd("chmod +x /mnt/gentoo/setup_chroot.sh");

    printf("Executing setup inside chroot...\n");
    execute_cmd_abort("chroot /mnt/gentoo /setup_chroot.sh", "Chroot setup script failed");

    printf("Cleaning up...\n");
    execute_cmd("rm -f /mnt/gentoo/setup_chroot.sh");
    execute_cmd("rm -f /mnt/gentoo/stage3.tar.xz");
    execute_cmd("umount -l /mnt/gentoo/dev/pts 2>/dev/null");
    execute_cmd("umount -l /mnt/gentoo/dev/shm 2>/dev/null");
    execute_cmd("umount -R /mnt/gentoo 2>/dev/null");

    printf("\n=======================================================\n");
    printf("Gentoo Linux installation completed successfully!\n");
    printf("You can now safely reboot your system.\n");
    printf("=======================================================\n");
    
    return 0;
}
