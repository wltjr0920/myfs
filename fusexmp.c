/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>
  Copyright (C) 2011       Sebastian Pipping <sebastian@pipping.org>

  This program can be distributed under the terms of the GNU GPL.
  See the file COPYING.

  gcc -Wall fusexmp.c `pkg-config fuse --cflags --libs` -o fusexmp
*/
#define STRIPE 512
#define FUSE_USE_VERSION 26

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef linux
/* For pread()/pwrite()/utimensat() */
#define _XOPEN_SOURCE 700
#endif

#ifndef HAVE_UTIMENSAT
#define HAVE_UTIMENSAT
#endif

#ifndef HAVE_POSIX_FALLOCATE
#define HAVE_POSIX_FALLOCATE
#endif 

#ifndef HAVE_SETXATTR
#define HAVE_SETXATTR
#endif

#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <sys/time.h>
#ifdef HAVE_SETXATTR
#include <sys/xattr.h>
#endif
#include <linux/limits.h>


static struct {
  char driveA[512];
  char driveB[512];
} global_context;

static int xmp_getattr(const char *path, struct stat *stbuf)
{
  char fullpaths[2][PATH_MAX];
  struct stat sum;
	int res;

 sprintf(fullpaths[0], "%s%s", global_context.driveA, path);
 sprintf(fullpaths[1], "%s%s", global_context.driveB, path);

	res = lstat(fullpaths[0], stbuf);
	if (res == -1)
		return -errno;
  res = lstat(fullpaths[1], &sum);
	if (res == -1)
		return -errno;
  
  stbuf->st_size+=sum.st_size;
	return 0;
}

static int xmp_access(const char *path, int mask)
{
  char fullpaths[2][PATH_MAX];
	int res;

  sprintf(fullpaths[0], "%s%s", global_context.driveA, path);
  sprintf(fullpaths[1], "%s%s", global_context.driveB, path);

	res = access(fullpaths[0], mask);
	if (res == -1)
		return -errno;
  
  int sum = access(fullpaths[1],mask);
  if(sum==-1)
  return -errno;

	return 0;
}

static int xmp_readlink(const char *path, char *buf, size_t size)
{
  char fullpaths[2][PATH_MAX];
	int res;

  sprintf(fullpaths[0], "%s%s", global_context.driveA, path);
  sprintf(fullpaths[1], "%s%s", global_context.driveB, path);

	res = readlink(fullpaths[0], buf, size - 1);
	if (res == -1)
		return -errno;
res = readlink(fullpaths[1], buf, size - 1);
	if (res == -1)
		return -errno;

	buf[res] = '\0';
	return 0;
}


static int xmp_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
		       off_t offset, struct fuse_file_info *fi)
{
  char fullpaths[2][PATH_MAX];

	DIR *dp;
	struct dirent *de;

	(void) offset;
	(void) fi;

  sprintf(fullpaths[0], "%s%s", global_context.driveA, path);
  sprintf(fullpaths[1], "%s%s", global_context.driveB, path);

	dp = opendir(fullpaths[0]);
	if (dp == NULL)
		return -errno;

	while ((de = readdir(dp)) != NULL) {
		struct stat st;
		memset(&st, 0, sizeof(st));
		st.st_ino = de->d_ino;
		st.st_mode = de->d_type << 12;

		if (filler(buf, de->d_name, &st, 0))
			break;
	}

	closedir(dp);
	return 0;
}

static int xmp_mknod(const char *path, mode_t mode, dev_t rdev)
{
  char fullpaths[2][PATH_MAX];
	int res;

  sprintf(fullpaths[0], "%s%s", global_context.driveA, path);
  sprintf(fullpaths[1], "%s%s", global_context.driveB, path);

	/* On Linux this could just be 'mknod(path, mode, rdev)' but this
	   is more portable */
	
  for (int i = 0; i < 2; ++i) {
    const char* fullpath = fullpaths[i];

    if (S_ISREG(mode)) {
      res = open(fullpath, O_CREAT | O_EXCL | O_WRONLY, mode);
      if (res >= 0)
        res = close(res);
    } else if (S_ISFIFO(mode))
      res = mkfifo(fullpath, mode);
    else
      res = mknod(fullpath, mode, rdev);
    if (res == -1)
      return -errno;
  }

	return 0;
}

static int xmp_mkdir(const char *path, mode_t mode)
{
  char fullpaths[2][PATH_MAX];
	int res;

  sprintf(fullpaths[0], "%s%s", global_context.driveA, path);
  sprintf(fullpaths[1], "%s%s", global_context.driveB, path);

  for (int i = 0; i < 2; ++i) {
    const char* fullpath = fullpaths[i];

    res = mkdir(fullpath, mode);
    if (res == -1)
      return -errno;
  }

	return 0;
}

static int xmp_unlink(const char *path)
{
  char fullpaths[2][PATH_MAX];
	int res;

  sprintf(fullpaths[0], "%s%s", global_context.driveA, path);
  sprintf(fullpaths[1], "%s%s", global_context.driveB, path);

  for (int i = 0; i < 2; ++i) {
    const char* fullpath = fullpaths[i];
    res = unlink(fullpath);
    if (res == -1)
      return -errno;
  }

	return 0;
}

static int xmp_rmdir(const char *path)
{
  char fullpaths[2][PATH_MAX];
	int res;

  sprintf(fullpaths[0], "%s%s", global_context.driveA, path);
  sprintf(fullpaths[1], "%s%s", global_context.driveB, path);

  for (int i = 0; i < 2; ++i) {
    const char* fullpath = fullpaths[i];
    res = rmdir(fullpath);
    if (res == -1)
      return -errno;
  }

  return 0;
}

static int xmp_symlink(const char *from, const char *to)
{
  char read_fullpath[2][PATH_MAX];
  char write_fullpaths[2][PATH_MAX];
  int res;

  sprintf(read_fullpath[0], "%s%s", global_context.driveA, to);
  sprintf(read_fullpath[1], "%s%s", global_context.driveB, to);
  sprintf(write_fullpaths[0], "%s%s", global_context.driveA, to);
  sprintf(write_fullpaths[1], "%s%s", global_context.driveB, to);

  for (int i = 0; i < 2; ++i) {
    res = symlink(read_fullpath[i], write_fullpaths[i]);
    if (res == -1)
      return -errno;
  }

  return 0;
}

static int xmp_rename(const char *from, const char *to)
{
  char read_fullpath[2][PATH_MAX];
  char write_fullpaths[2][PATH_MAX];
  int res;

  sprintf(read_fullpath[0], "%s%s", global_context.driveA, to);
  sprintf(read_fullpath[1], "%s%s", global_context.driveB, to);
  sprintf(write_fullpaths[0], "%s%s", global_context.driveA, to);
  sprintf(write_fullpaths[1], "%s%s", global_context.driveB, to);

  for (int i = 0; i < 2; ++i) {
    res = rename(read_fullpath[i], write_fullpaths[i]);
    if (res == -1)
      return -errno;
  }

  return 0;
}

static int xmp_link(const char *from, const char *to)
{
  char read_fullpath[2][PATH_MAX];
  char write_fullpaths[2][PATH_MAX];
  int res;

  sprintf(read_fullpath[0], "%s%s", global_context.driveA, to);
  sprintf(read_fullpath[1], "%s%s", global_context.driveB, to);

  sprintf(write_fullpaths[0], "%s%s", global_context.driveA, to);
  sprintf(write_fullpaths[1], "%s%s", global_context.driveB, to);

  for (int i = 0; i < 2; ++i) {
    res = link(read_fullpath[i], write_fullpaths[i]);
    if (res == -1)
      return -errno;
  }

  return 0;
}

static int xmp_chmod(const char *path, mode_t mode)
{
  char fullpaths[2][PATH_MAX];
  int res;

  sprintf(fullpaths[0], "%s%s", global_context.driveA, path);
  sprintf(fullpaths[1], "%s%s", global_context.driveB, path);

  for (int i = 0; i < 2; ++i) {
    res = chmod(fullpaths[i], mode);
    if (res == -1)
      return -errno;
  }

  return 0;
}

static int xmp_chown(const char *path, uid_t uid, gid_t gid)
{
  char fullpaths[2][PATH_MAX];
  int res;

  sprintf(fullpaths[0], "%s%s", global_context.driveA, path);
  sprintf(fullpaths[1], "%s%s", global_context.driveB, path);

  for (int i = 0; i < 2; ++i) {
    res = lchown(fullpaths[i], uid, gid);
    if (res == -1)
      return -errno;
  }

  return 0;
}

static int xmp_truncate(const char *path, off_t size)
{
  char fullpaths[2][PATH_MAX];
  int res;

  sprintf(fullpaths[0], "%s%s", global_context.driveA, path);
  sprintf(fullpaths[1], "%s%s", global_context.driveB, path);

  for (int i = 0; i < 2; ++i) {
    res = truncate(fullpaths[i], size);
    if (res == -1)
      return -errno;
  }

  return 0;
}

#ifdef HAVE_UTIMENSAT
static int xmp_utimens(const char *path, const struct timespec ts[2])
{
  char fullpaths[2][PATH_MAX];
  int res;

  sprintf(fullpaths[0], "%s%s", global_context.driveA, path);
  sprintf(fullpaths[1], "%s%s", global_context.driveB, path);

  for (int i = 0; i < 2; ++i) {
    /* don't use utime/utimes since they follow symlinks */
    res = utimensat(0, fullpaths[i], ts, AT_SYMLINK_NOFOLLOW);
    if (res == -1)
      return -errno;
  }

  return 0;
}
#endif

static int xmp_open(const char *path, struct fuse_file_info *fi)
{
  char fullpaths[2][PATH_MAX];
  int res;
  struct fuse_file_info file2;
  sprintf(fullpaths[0], "%s%s", global_context.driveA, path);
  sprintf(fullpaths[1], "%s%s", global_context.driveB, path);

  res = open(fullpaths[0], fi->flags);
  if (res == -1)
    return -errno;
    res = open(fullpaths[1], (&file2)->flags);
  if (res == -1)
    return -errno;
  close(res);
  return 0;
}

static int xmp_read(const char *path, char *buf, size_t size, off_t offset,
    struct fuse_file_info *fi)
{ 
  char fullpaths[2][PATH_MAX];
  int fd;
  int res = 1;
  off_t block=offset/STRIPE;
  int sum=1;
  (void) fi;
  sprintf(fullpaths[0], "%s%s", global_context.driveA, path);
  sprintf(fullpaths[1], "%s%s", global_context.driveB, path);
  for(res = 1;sum; block++){
    fd = open(fullpaths[block%2], O_RDONLY);
    if (fd == -1)
      return -errno;
    sum = pread(fd, buf+block*STRIPE, STRIPE, block/2*STRIPE);
    res=res+sum;
    if (res == -1)
      res = -errno;

// fprintf(stdout,"fullpaths : %s\n",fullpaths[block%2]);
// fprintf(stdout,"size : %ld\n",size);
// fprintf(stdout,"offset : %ld\n",offset);
// fprintf(stdout,"block : %ld\n",block);
// fprintf(stdout,"buf : %s\n",buf);
// fprintf(stdout,"res : %d\n",res);
// fprintf(stdout,"size/STRIPE : %ld\n\n\n",size/STRIPE); 
    close(fd);
   
}
    return res;
}

static int xmp_write(const char *path, const char *buf, size_t size,
off_t offset, struct fuse_file_info *fi)
{
  char fullpaths[2][PATH_MAX];
  int fd;
  int res = 0;
  (void) fi;
  // char *temp=(char*)malloc(strlen(buf));
  sprintf(fullpaths[0], "%s%s", global_context.driveA, path);
  sprintf(fullpaths[1], "%s%s", global_context.driveB, path);

// strncpy(temp,buf,size);
  off_t block=offset/STRIPE;

    fd = open(fullpaths[block%2], O_WRONLY);//open file by offset/stripe
    if (fd == -1)
      return -errno;
if(STRIPE<size){
 res = pwrite(fd, buf, STRIPE, block/2*STRIPE-block*STRIPE+offset);
    if (res == -1)
      res = -errno;
    close(fd);
    // int ptr = strchr(buf,'\n');
    // while(ptr<strlen(buf)){
    //   memset(temp[ptr],0,STRIPE);
    //   strcpy(temp[ptr],'');
    //   ptr++;
    // // }
    //    fprintf(stdout, "STRIPE<size*******");
    //    fprintf(stdout, "path : %s     \n",fullpaths[block%2]);
    //     fprintf(stdout, "buf : %s     \n",buf);
    //     fprintf(stdout, "size : %ld     ",size);
    //     fprintf(stdout, "block : %ld     \n\n",block);
}else{
  res = pwrite(fd, buf, size, block/2*STRIPE-block*STRIPE+offset);
    if (res == -1)
      res = -errno;
    close(fd);
    // int ptr = strchr(buf,'\n');
    // while(ptr<strlen(buf)){
    //   memset(temp[ptr],0,STRIPE);
    //   ptr++;
    // }
      //  fprintf(stdout, "STRIPE>=size*******");
      //   fprintf(stdout, "path : %s     \n",fullpaths[block%2]);
      //   fprintf(stdout, "buf : %s     \n",buf);
      //   fprintf(stdout, "size : %ld     ",size);
      //   fprintf(stdout, "block : %ld     \n\n",block);
}

 return res;
}
static int xmp_statfs(const char *path, struct statvfs *stbuf)
{
  char fullpaths[2][PATH_MAX];
  int res;
  struct statvfs sum;

  
  sprintf(fullpaths[0], "%s%s", global_context.driveA, path);
  sprintf(fullpaths[1], "%s%s", global_context.driveB, path);
  res = statvfs(fullpaths[0], stbuf);
  if (res == -1)
    return -errno;
     res = statvfs(fullpaths[1], &sum);
  if (res == -1)
    return -errno;
  return 0;
}

static int xmp_release(const char *path, struct fuse_file_info *fi)
{
  /* Just a stub.	 This method is optional and can safely be left
     unimplemented */

  (void) path;
  (void) fi;
  return 0;
}

static int xmp_fsync(const char *path, int isdatasync,
    struct fuse_file_info *fi)
{
  /* Just a stub.	 This method is optional and can safely be left
     unimplemented */

  (void) path;
  (void) isdatasync;
  (void) fi;
  return 0;
}

#ifdef HAVE_POSIX_FALLOCATE
static int xmp_fallocate(const char *path, int mode,
    off_t offset, off_t length, struct fuse_file_info *fi)
{
  char fullpaths[2][PATH_MAX];
  int fd;
  int res;

  (void) fi;
  sprintf(fullpaths[0], "%s%s", global_context.driveA, path);
  sprintf(fullpaths[1], "%s%s", global_context.driveB, path);

  if (mode)
    return -EOPNOTSUPP;

  for (int i = 0; i < 2; ++i) {
    const char* fullpath = fullpaths[i];

    fd = open(fullpath, O_WRONLY);
    if (fd == -1)
      return -errno;

    res = -posix_fallocate(fd, offset, length);

    close(fd);
  }

  return res;
}
#endif

#ifdef HAVE_SETXATTR
/* xattr operations are optional and can safely be left unimplemented */
static int xmp_setxattr(const char *path, const char *name, const char *value,
    size_t size, int flags)
{
  char fullpaths[2][PATH_MAX];

  sprintf(fullpaths[0], "%s%s", global_context.driveA, path);
  sprintf(fullpaths[1], "%s%s", global_context.driveB, path);

  for (int i = 0; i < 2; ++i) {
    const char* fullpath = fullpaths[i];
    int res = lsetxattr(fullpath, name, value, size, flags);
    if (res == -1)
      return -errno;
  }

  return 0;
}

static int xmp_getxattr(const char *path, const char *name, char *value,
    size_t size)
{
  char fullpaths[2][PATH_MAX];

  sprintf(fullpaths[0], "%s%s", global_context.driveA, path);
  sprintf(fullpaths[1], "%s%s", global_context.driveB, path);

  int res = lgetxattr(fullpaths[0], name, value, size);
  if (res == -1)
    return -errno;
  int sum = lgetxattr(fullpaths[1], name, value, size);
  if (sum == -1)
    return -errno;
    res+=sum;
  return res;
}

static int xmp_listxattr(const char *path, char *list, size_t size)
{
  char fullpaths[2][PATH_MAX];

  sprintf(fullpaths[0], "%s%s", global_context.driveA, path);
  sprintf(fullpaths[1], "%s%s", global_context.driveB, path);

  int res = llistxattr(fullpaths[0], list, size);
  if (res == -1)
    return -errno;
  int sum = llistxattr(fullpaths[1],list,size);
  if(sum==-1)
  return -errno;

  res+=sum;

  return res;
}

static int xmp_removexattr(const char *path, const char *name)
{
  char fullpaths[2][PATH_MAX];

  sprintf(fullpaths[0], "%s%s", global_context.driveA, path);
  sprintf(fullpaths[1], "%s%s", global_context.driveB, path);

  for (int i = 0; i < 2; ++i) {
    const char* fullpath = fullpaths[i];
    int res = lremovexattr(fullpath, name);
    if (res == -1)
      return -errno;
  }

  return 0;
}
#endif /* HAVE_SETXATTR */

static struct fuse_operations xmp_oper = {
  .getattr	= xmp_getattr,
  .access		= xmp_access,
  .readlink	= xmp_readlink,
  .readdir	= xmp_readdir,
  .mknod		= xmp_mknod,
  .mkdir		= xmp_mkdir,
  .symlink	= xmp_symlink,
  .unlink		= xmp_unlink,
  .rmdir		= xmp_rmdir,
  .rename		= xmp_rename,
  .link		= xmp_link,
  .chmod		= xmp_chmod,
  .chown		= xmp_chown,
  .truncate	= xmp_truncate,
#ifdef HAVE_UTIMENSAT
  .utimens	= xmp_utimens,
#endif
  .open		= xmp_open,
  .read		= xmp_read,
  .write		= xmp_write,
  .statfs		= xmp_statfs,
  .release	= xmp_release,
  .fsync		= xmp_fsync,
#ifdef HAVE_POSIX_FALLOCATE
  .fallocate	= xmp_fallocate,
#endif
#ifdef HAVE_SETXATTR
  .setxattr	= xmp_setxattr,
  .getxattr	= xmp_getxattr,
  .listxattr	= xmp_listxattr,
  .removexattr	= xmp_removexattr,
#endif
};

int main(int argc, char *argv[])
{
  if (argc < 4) {
    fprintf(stderr, "usage: ./myfs <mount-point> <drive-A> <drive-B>\n");
    exit(1);
  }

  strcpy(global_context.driveB, argv[--argc]);
  strcpy(global_context.driveA, argv[--argc]);

  srand(time(NULL));

  umask(0);
  return fuse_main(argc, argv, &xmp_oper, NULL);
}
