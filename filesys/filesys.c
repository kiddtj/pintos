#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "threads/thread.h"

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();
  free_map_init ();

  if (format) 
    do_format ();

  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  free_map_close ();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size) 
{
  int i = 0;
  int prev = 0;
  while(name[i] != '\0'){
    if(name[i] == '/')
      prev = i;
    i++;
  }

  //set up for name parsing
  int min = strlen(&name[prev]);
  char *name_copy = calloc(min+1, sizeof(char));
  char *name_copy2 = calloc(strlen(name)+1, sizeof(char));
  if(name[prev] == '/')
    strlcpy(name_copy, &name[prev+1], min+1);
  else
    strlcpy(name_copy, &name[prev], min+1);
  strlcpy(name_copy2, name, strlen(name)+1);
  name_copy2[prev] = '\0';

  struct dir *dir;
  block_sector_t inode_sector = 0;
  //handle absolute and relative paths
  if(name[0]=='/'){
    dir = dir_open_root ();
  }
  else{
    dir = thread_current()->cur_directory;
  }

  struct inode *parent_of_target;
  if(name_copy2[0] !='\0') {
    if (!dir_lookup(dir, name_copy2, &parent_of_target)) {
      free(name_copy);
      free(name_copy2);
      return false;
    }
  dir = dir_open(parent_of_target);
  }
  

  uint32_t is_dir = 0;
  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size, is_dir)
                  && dir_add (dir, name_copy, inode_sector));
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);

  if(name[0] == '/'){
    dir_close (dir);
  }
  free(name_copy);
  free(name_copy2);
  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  struct dir *dir;
  if(name[0]=='/'){
    dir = dir_open_root ();
  }
  else{
    dir = thread_current()->cur_directory;
  }
  struct inode *inode = NULL;

  if (dir != NULL)
    dir_lookup (dir, name, &inode);
  
  if(inode == NULL)
    return NULL;

  if(name[0] == '/'){
    dir_close (dir);
  }

  return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
  struct dir *dir;
  //handle absolute and relative paths
  if(name[0]=='/'){
    dir = dir_open_root ();
  }
  else{
    dir = thread_current()->cur_directory;
  }
  int i = 0;
  int prev = 0;
  while(name[i] != '\0'){
    if(name[i] == '/')
      prev = i;
    i++;
  }

  //set up for name parsing
  int min = strlen(&name[prev]);
  char *name_copy = calloc(min+1, sizeof(char));
  char *name_copy2 = calloc(strlen(name)+1, sizeof(char));
  if(name[prev] == '/')
    strlcpy(name_copy, &name[prev+1], min+1);
  else
    strlcpy(name_copy, &name[prev], min+1);
  strlcpy(name_copy2, name, strlen(name)+1);
  name_copy2[prev] = '\0';

  block_sector_t inode_sector = 0;

  struct inode *parent_of_target;
  if(name_copy2[0] !='\0') {
    if (!dir_lookup(dir, name_copy2, &parent_of_target)) {
      free(name_copy);
      free(name_copy2);
      return false;
    }
    dir = dir_open(parent_of_target);
  }


  bool success = dir != NULL && dir_remove (dir, name_copy);
  if(name[0] == '/'){
    dir_close (dir); 
  }

  free(name_copy);
  free(name_copy2);
  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}
