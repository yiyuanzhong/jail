Jail
====
## What is Jail?
Jail is a Linux namespace based container implementation.

Specially designed for circumstances that a physical Linux box (the host)
cannot be assigned additional IP addresses so that each of its guests can
utilize full namespace isolation by having their own network stacks. In
another word, CLONE_NEWNET is not suitable.

## Why do I need it?
There are certain applications require fixed port, mostly system wide daemons.
SMTP for instance, must bind to port 25, there's no workaround since it's an
Internet standard. Although SSH can work with alternate ports, it's always
convenient to run it with default port 22.

Another important reason is that setns() to a new PID namespace is not possible
before Linux 3.11, once CLONE_NEWPID is established, there's no way to enter
the same container.

## How does it do?
By placing a "trojan horse" inside the container. Containers with CLONE_NEWPID
need a new init process, which is critical for container to work. By providing
init process, Jail can also get in touch with the parent namespace. A unix
socket is created for each container, in a path that child namespace cannot see.
A local client can communicate to gain access into the container, just like
the way you SSH from a remote machine. With this implementation, Jail can work
with any future namespace based container technology.

## Who can use it?
Since namespace doesn't require root privilege, nor does Jail. However certain
low level virtualization does require root privilege, like mount(2) or chroot(2).
Also if you plan to convince remote SSH to connect directly into a container,
you need root privilege.

## The catch?
The network stack is not isolated, so all containers see the network adapters
of the host, and they must be assigned ports wisely so they don't try to bind
on the same port.

## How to use it?
Jail has a server part (jaild) and a client part (jail). Environment must be
prepared beforehand: untar your new root file system, create proper user
accounts and assign correct password/pubkey, set reasonable Cgroups, anything
you need.

In this example, I place a CentOS root file system in __/home/services/centos__,
inside which has a user account __work__ with UID=61000, GID=61000.
```shell
$ tail /home/services/centos/etc/passwd
work:x:61000:61000:work:/home/work:/bin/bash
```

And a Debian in __/home/services/debian__, with
```shell
$ tail /home/services/debian/etc/passwd
work:x:61001:61001:work:/home/work:/bin/bash
```

I also create a user account __centos__ in the host box, with UID=61000 as well.
```shell
$ tail /etc/passwd
centos:x:61000:61000::/home/services/centos/home/work:/root/jail/bin/jail
debian:x:61001:61001::/home/services/debian/home/work:/root/jail/bin/jail
```

Since I'm demonstrating chroot(2) and mount(2), I need to be __root__.
```shell
$ su -
#
```

Now activate __jaild__ and see what happens.
```shell
# /root/jail/bin/jaild -u 61000 -r /home/services/centos
# su - centos
[work@localhost ~]$ id
uid=61000(work) gid=61000(work) groups=61000(work)
[work@localhost ~]$ cat /etc/issue
CentOS release 6.4 (Final)
Kernel \r on an \m

[work@localhost ~]$ ps -fj ax
UID        PID  PPID  PGID   SID  C STIME TTY      STAT   TIME CMD
root         1     0     1     1  0 23:11 ?        Ss     0:00 jaild: master [61000:61000]
work         2     1     1     1  0 23:11 ?        R      0:00 jaild: [3] /dev/pts/2
work         3     2     3     3  0 23:11 pts/2    Ss     0:00 -bash
work        18     3    18     3  0 23:11 pts/2    R+     0:00 ps -fj ax
[work@localhost ~]$ pwd
/home/work
[work@localhost ~]$ ls
centos.txt
[work@localhost ~]$
```

Why not try debian as well?
```shell
# /root/jail/bin/jaild -u 61001 -r /home/services/debian
# su - debian
work@localhost:~$ id
uid=61001(work) gid=61001(work) groups=61001(work)
work@localhost:~$ cat /etc/issue
Debian GNU/Linux 7 \n \l

work@localhost:~$ pwd
/home/work
work@localhost:~$ ls
debian.txt
work@localhost:~$
```

You see, both the containers think that it's running on __work__ account in
__/home/work__, perfect isolation.

Wanna see a remote SSH session?
```shell
localhost:~# ssh centos@localhost
centos@localhost's password:
Linux localhost.localdomain 3.2.0-4-amd64 #1 SMP Debian 3.2.51-1 x86_64

The programs included with the Debian GNU/Linux system are free software;
the exact distribution terms for each program are described in the
individual files in /usr/share/doc/*/copyright.

Debian GNU/Linux comes with ABSOLUTELY NO WARRANTY, to the extent
permitted by applicable law.
Last login: Sun Jul 28 13:34:39 2013 from ::1
[work@localhost ~]$ id
uid=61000(work) gid=61000(work) groups=61000(work)
[work@localhost ~]$
```

And yes, since it's the same container, it doesn't matter whether you su or ssh
to __centos__ account, you see processes created by each other, and files
created by each other.
