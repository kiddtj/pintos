#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "devices/shutdown.h"
#include "filesys/inode.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "filesys/directory.h"
#include "lib/user/syscall.h"

static void syscall_handler (struct intr_frame *);

/*pops front element from the process's stack and updates the stackpointer*/
#define POP_ESP(type) *(type*) esp; esp = ((type *) esp)+1

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

/*checks if the address is valid*/
int addr_is_good (void *a) 
{
	return a!=NULL && is_user_vaddr(a) && 
  pagedir_get_page (thread_current()->pagedir, a);
}

/*passes pointer to add_is_good to make sure that the address is valid, 
  exits the thread if not*/
void check_arg (void *a) 
{
	if (!addr_is_good(a)) {
    thread_current()->exit_status = -1;
		thread_exit();
  	}
}

/*translates the user address */
void *uservtop (void *uaddr, struct thread *t) 
{
  return pagedir_get_page(thread_current()->pagedir, uaddr);
}

static void
syscall_handler (struct intr_frame *f) 
{
  struct thread *t = thread_current();
  void *esp = f->esp;
  check_arg(esp);
  int32_t call_num = POP_ESP(int32_t);

  /*switches based on syscall interrupt number*/
  switch(call_num){

      case SYS_WAIT: {
      /*getting args*/
      check_arg(esp);
      int pid = POP_ESP(int);
      
      f->eax = process_wait(pid);
      return;
    }

    case SYS_OPEN: {
      /*getting args*/
    	check_arg(esp);
    	char *filename = POP_ESP(char*);
      check_arg(filename);

            int i;
      if(filename[0] == '.' && filename[1] == '\0'){
        struct dir *dir = dir_open(inode_reopen(t->cur_directory->inode));
        for(i = 2; i< MAX_FILES; i++){
          if(t->open_files[i] == NULL){
            t->open_files[i] = dir;
            f->eax = i;
                        return;
          }
        }
      }

      if(filename[0] == '.' && filename[1] == '.' && filename[2] == '\0'){
        struct dir *dir = dir_open(inode_reopen(t->cur_directory->inode));
        struct inode *inode = inode_open(dir->inode->data.parent_directory);
        struct dir *parent = dir_open(inode_reopen(inode)); 
        dir_close(dir);
        for(i = 2; i< MAX_FILES; i++){
          if(t->open_files[i] == NULL){
            t->open_files[i] = parent;
            f->eax = i;
                        return;
          }
        }
      }

    	struct file *new_file = filesys_open(filename);
      if(new_file == NULL){
                f->eax = -1;
        return;
      }

      if(new_file->inode->data.is_dir){
        struct dir *dir = dir_open(new_file->inode);
        for(i = 2; i< MAX_FILES; i++){
          if(t->open_files[i] == NULL){
            t->open_files[i] = dir;
            f->eax = i;
                        return;
          }
        }
      }
      else{
        for(i = 2; i< MAX_FILES; i++){
          if(t->open_files[i] == NULL){
            t->open_files[i] = new_file;
            f->eax = i;
                        return;
          }
        }
      }
    }

    case SYS_EXEC: {	
      /*getting args*/
      check_arg(esp);
      char *cmd_line = POP_ESP(char*);
      check_arg(cmd_line);

      tid_t child_pid = process_execute(cmd_line);
      if (child_pid == TID_ERROR){
        f->eax = -1;
        return;
      }
      f->eax = child_pid;
      return;
    }

    case SYS_HALT: {	
      shutdown_power_off();
      return;
    }

    case SYS_EXIT: {
      /*getting args*/	
    	check_arg(esp);
    	int arg1 = POP_ESP(int);

    	t->exit_status = arg1;
    	f->eax = arg1;
      thread_exit();
    	return;
    }

    case SYS_CREATE: {	
      /*getting args*/
      check_arg(esp);
      char * name = POP_ESP(char*);
      check_arg(name);
      check_arg(esp);
      unsigned size = POP_ESP(unsigned);

            f->eax = filesys_create(name, size);
            return;
    }

    case SYS_REMOVE: {
      /*getting args*/	
      check_arg(esp);
      char * name = POP_ESP(char*);
      check_arg(name);

            f->eax = filesys_remove(name);

            return;
    }

    case SYS_FILESIZE: {
      /*getting args*/	
      check_arg(esp);
      int fd = POP_ESP(int);
      
      if(fd >= MAX_FILES || fd<0){
        f->eax = 0;
        return;
      }
      struct file *file = t->open_files[fd];
      if(file == 0) {
        f->eax = 0;
      }
      else {
                f->eax = file_length(file);
              }

      return;
    }

    case SYS_READ: {	
      /*getting args*/
      check_arg(esp);
      int fd = POP_ESP(int);
      if(fd>=MAX_FILES || fd<0){
        f->eax = -1;
        return;
      }
      check_arg(esp);
      char *buffer = POP_ESP(void*);
      check_arg(buffer);
      check_arg(esp);
      unsigned size = POP_ESP(unsigned);

      if(0 == fd){
        int read = 0;
        for(;read<=size; read++){
          *buffer = input_getc();
          buffer++;
        }
        f->eax = size;
        return;
      }
      
      else{
               if(0 == t->open_files[fd]){
          f->eax = -1;
                  return;
        }
        f->eax = file_read(t->open_files[fd], buffer, size);
              return;
      }
      return;
    }

    case SYS_WRITE: {	
      /*getting args*/
    	check_arg(esp);
    	int fd = POP_ESP(int);
      if(fd>=MAX_FILES || fd<0){
        f->eax = 0;
        return;
      }
    	check_arg(esp);
    	void *buffer = POP_ESP(void*);
    	check_arg(buffer);
    	check_arg(esp);
    	unsigned size = POP_ESP(unsigned);

      if(fd == 1){
        putbuf(uservtop(buffer, t), size);
        f->eax = size;
      }
      else{
        if(t->open_files[fd] == NULL){
          f->eax = 0;
          return;
        }
                f->eax = file_write(t->open_files[fd], uservtop(buffer, t), size);
              }
      return;

    }

    case SYS_SEEK: {	
      /*getting args*/
      check_arg(esp);
      int fd = POP_ESP(int);
      if(fd>=MAX_FILES || fd<0){
        return;
      }
      struct file *file = t->open_files[fd];
      check_arg(esp);
      int pos = POP_ESP(int);

            file_seek(file, pos);
            return;
    }

    case SYS_TELL: {	
      /*getting args*/
      check_arg(esp);
      int fd = POP_ESP(int);
      
      if(fd>=MAX_FILES || fd<0){
        f->eax = 0;
        return;
      }
      struct file *file = t->open_files[fd];
            f->eax = file_tell(file);
            return;
    }

    case SYS_CLOSE: {	
      /*getting args*/
      check_arg(esp);
      int fd = POP_ESP(int);
     
      if(fd>=MAX_FILES || fd<2 || t->open_files[fd] == 0){
        return;
      }
            if (t->open_files[fd]->inode->data.is_dir) {
        struct dir *dirname = (struct dir *)t->open_files[fd];
        if(dirname == NULL){
                    return;
        }
        dir_close(dirname);
                t->open_files[fd] = 0;
      }
      else {
        struct file *filename = t->open_files[fd];
        if(filename == NULL){
                    return;
        }
        file_close(filename);
                t->open_files[fd] = 0;
      }
      return;
    }

    case SYS_CHDIR: {
      check_arg(esp);
      char *name = POP_ESP(char *);
      check_arg(name);
            //change directory
      f->eax = change_dir(name);
            return;
    }

    case SYS_MKDIR: {
      check_arg(esp);
      char *name = POP_ESP(char *);
      check_arg(name);
            //dir_create;
      f->eax = dir_make(name);
            return;
    }

    case SYS_READDIR: {
      /*getting args*/
      check_arg(esp);
      int fd = POP_ESP(int);
      if(fd>=MAX_FILES || fd<0){
        f->eax = -1;
        return;
      }
      check_arg(esp);
      char *name = POP_ESP(void*);
      check_arg(name);
      check_arg(esp);

            if(!t->open_files[fd]->inode->data.is_dir){
        f->eax = false;
                return;
      }
      struct dir *dir = (struct dir *) t->open_files[fd];
      if(dir == NULL || !dir->inode->data.is_dir){
                f->eax = false;
        return;
      }
      f->eax = dir_readdir(dir, name);
            return;
    }

    case SYS_ISDIR: {
      check_arg(esp);
      int fd = POP_ESP(int);
            struct file *filename = t->open_files[fd];
      if(filename == NULL){
                f->eax = false;
        return;
      }
      bool is_dir = filename->inode->data.is_dir;
      f->eax = is_dir;
      return;
    }

    case SYS_INUMBER: {
      check_arg(esp);
      int fd = POP_ESP(int);
      struct file *filename = t->open_files[fd];
      if(filename == NULL){
        f->eax = -1;
        return;
      }
      int sector = filename->inode->sector;
      f->eax = sector;
      return;

    }

    default:	{
    	printf ("unknown system call (%d)!\n", call_num);
      /*unknown system call is an error*/
      t->exit_status = -1;
    	thread_exit ();
      return;
    }
  }
}
