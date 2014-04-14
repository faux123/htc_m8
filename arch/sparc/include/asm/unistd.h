#ifndef _SPARC_UNISTD_H
#define _SPARC_UNISTD_H

/*
 * System calls under the Sparc.
 *
 * Don't be scared by the ugly clobbers, it is the only way I can
 * think of right now to force the arguments into fixed registers
 * before the trap into the system call with gcc 'asm' statements.
 *
 * Copyright (C) 1995, 2007 David S. Miller (davem@davemloft.net)
 *
 * SunOS compatibility based upon preliminary work which is:
 *
 * Copyright (C) 1995 Adrian M. Rodriguez (adrian@remus.rutgers.edu)
 */
#ifndef __32bit_syscall_numbers__
#ifndef __arch64__
#define __32bit_syscall_numbers__
#endif
#endif

#define __NR_restart_syscall      0 
#define __NR_exit                 1 
#define __NR_fork                 2 
#define __NR_read                 3 
#define __NR_write                4 
#define __NR_open                 5 
#define __NR_close                6 
#define __NR_wait4                7 
#define __NR_creat                8 
#define __NR_link                 9 
#define __NR_unlink              10 
#define __NR_execv               11 
#define __NR_chdir               12 
#define __NR_chown		 13 
#define __NR_mknod               14 
#define __NR_chmod               15 
#define __NR_lchown              16 
#define __NR_brk                 17 
#define __NR_perfctr             18 
#define __NR_lseek               19 
#define __NR_getpid              20 
#define __NR_capget		 21 
#define __NR_capset		 22 
#define __NR_setuid              23 
#define __NR_getuid              24 
#define __NR_vmsplice	         25 
#define __NR_ptrace              26 
#define __NR_alarm               27 
#define __NR_sigaltstack	 28 
#define __NR_pause               29 
#define __NR_utime               30 
#ifdef __32bit_syscall_numbers__
#define __NR_lchown32            31 
#define __NR_fchown32            32 
#endif
#define __NR_access              33 
#define __NR_nice                34 
#ifdef __32bit_syscall_numbers__
#define __NR_chown32             35 
#endif
#define __NR_sync                36 
#define __NR_kill                37 
#define __NR_stat                38 
#define __NR_sendfile		 39 
#define __NR_lstat               40 
#define __NR_dup                 41 
#define __NR_pipe                42 
#define __NR_times               43 
#ifdef __32bit_syscall_numbers__
#define __NR_getuid32            44 
#endif
#define __NR_umount2             45 
#define __NR_setgid              46 
#define __NR_getgid              47 
#define __NR_signal              48 
#define __NR_geteuid             49 
#define __NR_getegid             50 
#define __NR_acct                51 
#ifdef __32bit_syscall_numbers__
#define __NR_getgid32            53 
#else
#define __NR_memory_ordering	 52 
#endif
#define __NR_ioctl               54 
#define __NR_reboot              55 
#ifdef __32bit_syscall_numbers__
#define __NR_mmap2		 56 
#endif
#define __NR_symlink             57 
#define __NR_readlink            58 
#define __NR_execve              59 
#define __NR_umask               60 
#define __NR_chroot              61 
#define __NR_fstat               62 
#define __NR_fstat64		 63 
#define __NR_getpagesize         64 
#define __NR_msync               65 
#define __NR_vfork               66 
#define __NR_pread64             67 
#define __NR_pwrite64            68 
#ifdef __32bit_syscall_numbers__
#define __NR_geteuid32           69 
#define __NR_getegid32           70 
#endif
#define __NR_mmap                71 
#ifdef __32bit_syscall_numbers__
#define __NR_setreuid32          72 
#endif
#define __NR_munmap              73 
#define __NR_mprotect            74 
#define __NR_madvise             75 
#define __NR_vhangup             76 
#ifdef __32bit_syscall_numbers__
#define __NR_truncate64		 77 
#endif
#define __NR_mincore             78 
#define __NR_getgroups           79 
#define __NR_setgroups           80 
#define __NR_getpgrp             81 
#ifdef __32bit_syscall_numbers__
#define __NR_setgroups32         82 
#endif
#define __NR_setitimer           83 
#ifdef __32bit_syscall_numbers__
#define __NR_ftruncate64	 84 
#endif
#define __NR_swapon              85 
#define __NR_getitimer           86 
#ifdef __32bit_syscall_numbers__
#define __NR_setuid32            87 
#endif
#define __NR_sethostname         88 
#ifdef __32bit_syscall_numbers__
#define __NR_setgid32            89 
#endif
#define __NR_dup2                90 
#ifdef __32bit_syscall_numbers__
#define __NR_setfsuid32          91 
#endif
#define __NR_fcntl               92 
#define __NR_select              93 
#ifdef __32bit_syscall_numbers__
#define __NR_setfsgid32          94 
#endif
#define __NR_fsync               95 
#define __NR_setpriority         96 
#define __NR_socket              97 
#define __NR_connect             98 
#define __NR_accept              99 
#define __NR_getpriority        100 
#define __NR_rt_sigreturn       101 
#define __NR_rt_sigaction       102 
#define __NR_rt_sigprocmask     103 
#define __NR_rt_sigpending      104 
#define __NR_rt_sigtimedwait    105 
#define __NR_rt_sigqueueinfo    106 
#define __NR_rt_sigsuspend      107 
#ifdef __32bit_syscall_numbers__
#define __NR_setresuid32        108 
#define __NR_getresuid32        109 
#define __NR_setresgid32        110 
#define __NR_getresgid32        111 
#define __NR_setregid32         112 
#else
#define __NR_setresuid          108 
#define __NR_getresuid          109 
#define __NR_setresgid          110 
#define __NR_getresgid          111 
#endif
#define __NR_recvmsg            113 
#define __NR_sendmsg            114 
#ifdef __32bit_syscall_numbers__
#define __NR_getgroups32        115 
#endif
#define __NR_gettimeofday       116 
#define __NR_getrusage          117 
#define __NR_getsockopt         118 
#define __NR_getcwd		119 
#define __NR_readv              120 
#define __NR_writev             121 
#define __NR_settimeofday       122 
#define __NR_fchown             123 
#define __NR_fchmod             124 
#define __NR_recvfrom           125 
#define __NR_setreuid           126 
#define __NR_setregid           127 
#define __NR_rename             128 
#define __NR_truncate           129 
#define __NR_ftruncate          130 
#define __NR_flock              131 
#define __NR_lstat64		132 
#define __NR_sendto             133 
#define __NR_shutdown           134 
#define __NR_socketpair         135 
#define __NR_mkdir              136 
#define __NR_rmdir              137 
#define __NR_utimes             138 
#define __NR_stat64		139 
#define __NR_sendfile64         140 
#define __NR_getpeername        141 
#define __NR_futex              142 
#define __NR_gettid             143 
#define __NR_getrlimit		144 
#define __NR_setrlimit          145 
#define __NR_pivot_root		146 
#define __NR_prctl		147 
#define __NR_pciconfig_read	148 
#define __NR_pciconfig_write	149 
#define __NR_getsockname        150 
#define __NR_inotify_init       151 
#define __NR_inotify_add_watch  152 
#define __NR_poll               153 
#define __NR_getdents64		154 
#ifdef __32bit_syscall_numbers__
#define __NR_fcntl64		155 
#endif
#define __NR_inotify_rm_watch   156 
#define __NR_statfs             157 
#define __NR_fstatfs            158 
#define __NR_umount             159 
#define __NR_sched_set_affinity 160 
#define __NR_sched_get_affinity 161 
#define __NR_getdomainname      162 
#define __NR_setdomainname      163 
#ifndef __32bit_syscall_numbers__
#define __NR_utrap_install	164 
#endif
#define __NR_quotactl           165 
#define __NR_set_tid_address    166 
#define __NR_mount              167 
#define __NR_ustat              168 
#define __NR_setxattr           169 
#define __NR_lsetxattr          170 
#define __NR_fsetxattr          171 
#define __NR_getxattr           172 
#define __NR_lgetxattr          173 
#define __NR_getdents           174 
#define __NR_setsid             175 
#define __NR_fchdir             176 
#define __NR_fgetxattr          177 
#define __NR_listxattr          178 
#define __NR_llistxattr         179 
#define __NR_flistxattr         180 
#define __NR_removexattr        181 
#define __NR_lremovexattr       182 
#define __NR_sigpending         183 
#define __NR_query_module	184 
#define __NR_setpgid            185 
#define __NR_fremovexattr       186 
#define __NR_tkill              187 
#define __NR_exit_group		188 
#define __NR_uname              189 
#define __NR_init_module        190 
#define __NR_personality        191 
#define __NR_remap_file_pages   192 
#define __NR_epoll_create       193 
#define __NR_epoll_ctl          194 
#define __NR_epoll_wait         195 
#define __NR_ioprio_set         196 
#define __NR_getppid            197 
#define __NR_sigaction          198 
#define __NR_sgetmask           199 
#define __NR_ssetmask           200 
#define __NR_sigsuspend         201 
#define __NR_oldlstat           202 
#define __NR_uselib             203 
#define __NR_readdir            204 
#define __NR_readahead          205 
#define __NR_socketcall         206 
#define __NR_syslog             207 
#define __NR_lookup_dcookie     208 
#define __NR_fadvise64          209 
#define __NR_fadvise64_64       210 
#define __NR_tgkill             211 
#define __NR_waitpid            212 
#define __NR_swapoff            213 
#define __NR_sysinfo            214 
#define __NR_ipc                215 
#define __NR_sigreturn          216 
#define __NR_clone              217 
#define __NR_ioprio_get         218 
#define __NR_adjtimex           219 
#define __NR_sigprocmask        220 
#define __NR_create_module      221 
#define __NR_delete_module      222 
#define __NR_get_kernel_syms    223 
#define __NR_getpgid            224 
#define __NR_bdflush            225 
#define __NR_sysfs              226 
#define __NR_afs_syscall        227 
#define __NR_setfsuid           228 
#define __NR_setfsgid           229 
#define __NR__newselect         230 
#ifdef __32bit_syscall_numbers__
#define __NR_time               231 
#else
#ifdef __KERNEL__
#define __NR_time		231 
#endif
#endif
#define __NR_splice             232 
#define __NR_stime              233 
#define __NR_statfs64           234 
#define __NR_fstatfs64          235 
#define __NR__llseek            236 
#define __NR_mlock              237
#define __NR_munlock            238
#define __NR_mlockall           239
#define __NR_munlockall         240
#define __NR_sched_setparam     241
#define __NR_sched_getparam     242
#define __NR_sched_setscheduler 243
#define __NR_sched_getscheduler 244
#define __NR_sched_yield        245
#define __NR_sched_get_priority_max 246
#define __NR_sched_get_priority_min 247
#define __NR_sched_rr_get_interval  248
#define __NR_nanosleep          249
#define __NR_mremap             250
#define __NR__sysctl            251
#define __NR_getsid             252
#define __NR_fdatasync          253
#define __NR_nfsservctl         254
#define __NR_sync_file_range	255
#define __NR_clock_settime	256
#define __NR_clock_gettime	257
#define __NR_clock_getres	258
#define __NR_clock_nanosleep	259
#define __NR_sched_getaffinity	260
#define __NR_sched_setaffinity	261
#define __NR_timer_settime	262
#define __NR_timer_gettime	263
#define __NR_timer_getoverrun	264
#define __NR_timer_delete	265
#define __NR_timer_create	266
#define __NR_io_setup		268
#define __NR_io_destroy		269
#define __NR_io_submit		270
#define __NR_io_cancel		271
#define __NR_io_getevents	272
#define __NR_mq_open		273
#define __NR_mq_unlink		274
#define __NR_mq_timedsend	275
#define __NR_mq_timedreceive	276
#define __NR_mq_notify		277
#define __NR_mq_getsetattr	278
#define __NR_waitid		279
#define __NR_tee		280
#define __NR_add_key		281
#define __NR_request_key	282
#define __NR_keyctl		283
#define __NR_openat		284
#define __NR_mkdirat		285
#define __NR_mknodat		286
#define __NR_fchownat		287
#define __NR_futimesat		288
#define __NR_fstatat64		289
#define __NR_unlinkat		290
#define __NR_renameat		291
#define __NR_linkat		292
#define __NR_symlinkat		293
#define __NR_readlinkat		294
#define __NR_fchmodat		295
#define __NR_faccessat		296
#define __NR_pselect6		297
#define __NR_ppoll		298
#define __NR_unshare		299
#define __NR_set_robust_list	300
#define __NR_get_robust_list	301
#define __NR_migrate_pages	302
#define __NR_mbind		303
#define __NR_get_mempolicy	304
#define __NR_set_mempolicy	305
#define __NR_kexec_load		306
#define __NR_move_pages		307
#define __NR_getcpu		308
#define __NR_epoll_pwait	309
#define __NR_utimensat		310
#define __NR_signalfd		311
#define __NR_timerfd_create	312
#define __NR_eventfd		313
#define __NR_fallocate		314
#define __NR_timerfd_settime	315
#define __NR_timerfd_gettime	316
#define __NR_signalfd4		317
#define __NR_eventfd2		318
#define __NR_epoll_create1	319
#define __NR_dup3		320
#define __NR_pipe2		321
#define __NR_inotify_init1	322
#define __NR_accept4		323
#define __NR_preadv		324
#define __NR_pwritev		325
#define __NR_rt_tgsigqueueinfo	326
#define __NR_perf_event_open	327
#define __NR_recvmmsg		328
#define __NR_fanotify_init	329
#define __NR_fanotify_mark	330
#define __NR_prlimit64		331
#define __NR_name_to_handle_at	332
#define __NR_open_by_handle_at	333
#define __NR_clock_adjtime	334
#define __NR_syncfs		335
#define __NR_sendmmsg		336
#define __NR_setns		337
#define __NR_process_vm_readv	338
#define __NR_process_vm_writev	339

#define NR_syscalls		340

#ifdef __32bit_syscall_numbers__
#define __IGNORE_setresuid
#define __IGNORE_getresuid
#define __IGNORE_setresgid
#define __IGNORE_getresgid
#endif

#ifdef __KERNEL__
#define __ARCH_WANT_IPC_PARSE_VERSION
#define __ARCH_WANT_OLD_READDIR
#define __ARCH_WANT_STAT64
#define __ARCH_WANT_SYS_ALARM
#define __ARCH_WANT_SYS_GETHOSTNAME
#define __ARCH_WANT_SYS_PAUSE
#define __ARCH_WANT_SYS_SGETMASK
#define __ARCH_WANT_SYS_SIGNAL
#define __ARCH_WANT_SYS_TIME
#define __ARCH_WANT_SYS_UTIME
#define __ARCH_WANT_SYS_WAITPID
#define __ARCH_WANT_SYS_SOCKETCALL
#define __ARCH_WANT_SYS_FADVISE64
#define __ARCH_WANT_SYS_GETPGRP
#define __ARCH_WANT_SYS_LLSEEK
#define __ARCH_WANT_SYS_NICE
#define __ARCH_WANT_SYS_OLDUMOUNT
#define __ARCH_WANT_SYS_SIGPENDING
#define __ARCH_WANT_SYS_SIGPROCMASK
#define __ARCH_WANT_SYS_RT_SIGSUSPEND
#ifdef __32bit_syscall_numbers__
#define __ARCH_WANT_SYS_IPC
#else
#define __ARCH_WANT_COMPAT_SYS_TIME
#define __ARCH_WANT_COMPAT_SYS_RT_SIGSUSPEND
#endif

#define cond_syscall(x) asm(".weak\t" #x "\n\t.set\t" #x ",sys_ni_syscall")

#endif 
#endif 
