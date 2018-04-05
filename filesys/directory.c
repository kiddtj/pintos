#include "filesys/directory.h"
#include <stdio.h>
#include <string.h>
#include <list.h>
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "filesys/free-map.h"
#include "threads/synch.h"



/* A single directory entry. */
struct dir_entry 
  {
    block_sector_t inode_sector;        /* Sector number of header. */
    char name[NAME_MAX + 1];            /* Null terminated file name. */
    bool in_use;                        /* In use or free? */
  };


//changes current thread's current directory to one with name name
bool
change_dir(char *name){
  if(name[0] == '\0'){
    return false;
  }
  struct dir *lookup_dir;
  struct thread *t = thread_current();
 
  bool do_free_dir = false;
  //absolute or relative path
  if (name[0]=='/') {
    lookup_dir = dir_open_root();
    do_free_dir = true;
  }
  else{
    lookup_dir = t->cur_directory;
  }

  struct inode *inode;
  inode = calloc(1, sizeof inode);
  if(!dir_lookup(lookup_dir, name, &inode)){
    free(inode);
    if(do_free_dir)
      dir_close(lookup_dir);
    return false;
  }
  if(do_free_dir)
    dir_close(lookup_dir);
  dir_close(t->cur_directory);
  t->cur_directory = dir_open(inode);
  return true;
}

//creates new directory with name name
bool
dir_make (char *name) {

  //name can't be empty
  if(name[0] == '\0')
    return false;

  struct dir *lookup_dir;
 
  bool do_free_dir = false;
  //absolute or relative path
  if (name[0]=='/') {
    lookup_dir = dir_open_root();
    do_free_dir = true;
  }
  else
    lookup_dir = thread_current()->cur_directory;


  struct inode *t;
  if (dir_lookup(lookup_dir, name, &t)) {
    if(name[0]=='/')
      dir_close(lookup_dir);
    inode_close(t);
    return false;
  }

  //parses name into the path to the parent directory and name of new directory
  int i = 0;
  int prev = 0;
  while(name[i] != '\0'){
    if(name[i] == '/')
      prev = i;
    i++;
  }

  int min = strlen(&name[prev]);
  char *name_copy = calloc(min+1, sizeof(char));
  char *name_copy2 = calloc(strlen(name)+1, sizeof(char));
  if(name[prev] == '/')
    strlcpy(name_copy, &name[prev+1], min+1);
  else
    strlcpy(name_copy, &name[prev], min+1);
  strlcpy(name_copy2, name, strlen(name)+1);
  name_copy2[prev] = '\0';
  //name_copy is the new directory's name
  //name copy2 is the parent directory's path

  struct dir_entry *entry = calloc(1, sizeof(struct dir_entry));
  strlcpy(entry->name, name_copy, strlen(name_copy)+1);
  free_map_allocate(1, &entry->inode_sector);
  dir_create(entry->inode_sector, 1);
  struct dir* dir;
  if(name_copy2[0] != '\0'){
    ASSERT(dir_lookup(lookup_dir, name_copy2, &t));
    dir = dir_open(t);
    dir_add(dir, name_copy, entry->inode_sector);
    dir_close(dir);

  }
  else{ //if the path is empty, parent is lookup_dir
    dir = lookup_dir;
    dir_add(dir, name_copy, entry->inode_sector);
  }

  //if absolute path
  if(name[0]=='/')
    dir_close(lookup_dir);
  free(name_copy2);
  free(name_copy);
  free(entry);
  return true;
}

/* Creates a directory with space for ENTRY_CNT entries in the
   given SECTOR.  Returns true if successful, false on failure. */
bool
dir_create (block_sector_t sector, size_t entry_cnt)
{
  uint32_t is_dir = 1;
  return inode_create (sector, entry_cnt * sizeof (struct dir_entry), is_dir);
}

/* Opens and returns the directory for the given INODE, of which
   it takes ownership.  Returns a null pointer on failure. */
struct dir *
dir_open (struct inode *inode) 
{
  struct dir *dir = calloc (1, sizeof *dir);
  if (inode != NULL && dir != NULL)
    {
      dir->inode = inode;
      dir->pos = 0;
      return dir;
    }
  else
    {
      inode_close (inode);
      free (dir);
      return NULL; 
    }
}

/* Opens the root directory and returns a directory for it.
   Return true if successful, false on failure. */
struct dir *
dir_open_root (void)
{
  return dir_open (inode_open (ROOT_DIR_SECTOR));
}

/* Opens and returns a new directory for the same inode as DIR.
   Returns a null pointer on failure. */
struct dir *
dir_reopen (struct dir *dir) 
{
  return dir_open (inode_reopen (dir->inode));
}

/* Destroys DIR and frees associated resources. */
void
dir_close (struct dir *dir) 
{
  if (dir != NULL)
    {
      inode_close (dir->inode);
      free (dir);
    }
}

/* Returns the inode encapsulated by DIR. */
struct inode *
dir_get_inode (struct dir *dir) 
{
  return dir->inode;
}

/* Searches DIR for a file with the given NAME.
   If successful, returns true, sets *EP to the directory entry
   if EP is non-null, and sets *OFSP to the byte offset of the
   directory entry if OFSP is non-null.
   otherwise, returns false and ignores EP and OFSP. */
static bool
lookup (const struct dir *dir, const char *name,
        struct dir_entry *ep, off_t *ofsp)
{
  struct dir_entry e;
  size_t ofs;
  
  ASSERT (dir != NULL);
  ASSERT (name != NULL);


  //special case for empty name or '.'
  if(name[0] == '\0' || (name[0] == '.' && name[1] == '\0')){

      struct dir *parent = dir_open(inode_open(dir->inode->data.parent_directory));
      if(parent==NULL) parent = dir_open_root();
      for (ofs = 0; inode_read_at (parent->inode, &e, sizeof e, ofs) == sizeof e;
          ofs += sizeof e) 
        if (e.in_use && dir->inode->sector == e.inode_sector) 
        {
          if (ep != NULL)
            *ep = e;
          if (ofsp != NULL)
            *ofsp = ofs;
          dir_close(parent);
          return true;
      }
    dir_close(parent);
    return false;
  }

  //special case for '..'
  else if(name[0] == '.' && name[1] == '.' && name[2] == '\0'){

      struct dir *parent = dir_open(inode_open(dir->inode->data.parent_directory));
      if(parent==NULL){
        parent = dir_open_root();
      }
      struct dir *grandparent = dir_open(inode_open(parent->inode->data.parent_directory));
      if(grandparent==NULL){
        grandparent = dir_open_root();
      }

      for (ofs = 0; inode_read_at (grandparent->inode, &e, sizeof e, ofs) == sizeof e;
          ofs += sizeof e) 
        if (e.in_use && parent->inode->sector == e.inode_sector) 
        {
          if (ep != NULL)
            *ep = e;
          if (ofsp != NULL)
            *ofsp = ofs;
          dir_close(parent);
          dir_close(grandparent);
          return true;
      }
    dir_close(parent);
    dir_close(grandparent);
    return false;
  }
  //searches dir for the file with a matching name
  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
    if (e.in_use && !strcmp (name, e.name)) 
      {
        if (ep != NULL)
          *ep = e;
        if (ofsp != NULL)
          *ofsp = ofs;
        return true;
      }
  return false;
}



/* Searches DIR for a file with the given NAME
   and returns true if one exists, false otherwise.
   On success, sets *INODE to an inode for the file, otherwise to
   a null pointer.  The caller must close *INODE. */

bool dir_lookup(const struct dir *dir, const char *name,
            struct inode **inode){
  struct dir_entry e;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  //checks for empty name
  if(name[0] == '\0')
    return false;

  if(name[0] == '/' && name[1] == '\0'){
    *inode = dir_open_root()->inode;
    return true;
  }

  if(name[0] == '.' && name[1] == '\0'){
    *inode = inode_open(dir->inode->sector);
    return true;
  }


  char *short_name = calloc(NAME_MAX + 1 , sizeof(char));
  strlcpy(short_name, name, NAME_MAX + 1);
  char *save_ptr;
  char *token;
  struct dir *directory = calloc(1, sizeof *directory);

  struct inode *new_inode = calloc(1, sizeof *new_inode);
  memcpy(new_inode, dir->inode, sizeof *new_inode);

  //finds inode at each level by looking up in previous level
  for (token = strtok_r (short_name, "/", &save_ptr); token; token = strtok_r (NULL, "/", &save_ptr)){
    if(token[0] == '\0')
      continue;
    directory = dir_open(new_inode);
    if(dir->inode->rw != NULL){
      read_acquire(&directory->inode->rw);
    }
    if (lookup (directory, token, &e, NULL)) {
      if(dir->inode->rw != NULL){
        read_release(&directory->inode->rw);
      }
      new_inode = inode_open(e.inode_sector);
      dir_close(directory);

    }
    else {
      if(dir->inode->rw != NULL){
        read_release(&directory->inode->rw);
      }
      dir_close(directory);
      new_inode = NULL;
      break;
    }
  }

  *inode = new_inode;

  return *inode != NULL;
}

/* Adds a file named NAME to DIR, which must not already contain a
   file by that name.  The file's inode is in sector
   INODE_SECTOR.
   Returns true if successful, false on failure.
   Fails if NAME is invalid (i.e. too long) or a disk or memory
   error occurs. */
bool
dir_add (struct dir *dir, const char *name, block_sector_t inode_sector)
{
  struct dir_entry e;
  off_t ofs;
  bool success = false;
  bool has_lock = false;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* Check NAME for validity. */
  if (*name == '\0' || strlen (name) > NAME_MAX)
    return false;

  /* Check that NAME is not in use. */
  read_acquire(&dir->inode->rw);
  if (lookup (dir, name, NULL, NULL)){
  read_release(&dir->inode->rw);
    goto done;
  }
  read_release(&dir->inode->rw);
  write_acquire(&dir->inode->rw);
  has_lock = true;

  /* Set OFS to offset of free slot.
     If there are no free slots, then it will be set to the
     current end-of-file.
     
     inode_read_at() will only return a short read at end of file.
     Otherwise, we'd need to verify that we didn't get a short
     read due to something intermittent such as low memory. */
  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
    if (!e.in_use)
      break;

  /* Write slot. */
  e.in_use = true;
  strlcpy (e.name, name, sizeof e.name);
  e.inode_sector = inode_sector;
  success = inode_write_at (dir->inode, &e, sizeof e, ofs) == sizeof e;

  done:;

  struct inode_disk *t;
  t = calloc(1,sizeof(struct inode_disk));
  block_read(fs_device, inode_sector, t);
  t->parent_directory = dir->inode->sector;
  block_write(fs_device, inode_sector, t);
  if (has_lock) write_release(&dir->inode->rw);
  free(t);
  return success;
}

/* Removes any entry for NAME in DIR.
   Returns true if successful, false on failure,
   which occurs only if there is no file with the given NAME. */
bool
dir_remove (struct dir *dir, const char *name) 
{
  struct dir_entry e;
  struct inode *inode = NULL;
  bool success = false;
  off_t ofs;
  bool has_lock = false;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* Find directory entry. */
  read_acquire(&dir->inode->rw);
  if (!lookup (dir, name, &e, &ofs)){
    read_release(&dir->inode->rw);
    goto done;
  }
  read_release(&dir->inode->rw);

  /* Open inode. */
  inode = inode_open (e.inode_sector);
  if (inode == NULL){
    goto done;
  }

  write_acquire(&inode->rw);
  has_lock = true;

  if (inode->data.is_dir && inode->open_cnt > 1) {
    goto done;
  }

  struct dir_entry ep;
  off_t ofsp = ofs;
  if(inode->data.is_dir) {
    for (ofsp = 0; inode_read_at (inode, &ep, sizeof ep, ofsp) == sizeof ep;
       ofsp += sizeof ep){ 
    if (ep.in_use) 
      {
        if (has_lock) write_release(&inode->rw);
        return false;
      }
    }
  }
  /* Erase directory entry. */
  e.in_use = false;
  if (inode_write_at (dir->inode, &e, sizeof e, ofs) != sizeof e) {
    goto done;
  }
  /* Remove inode. */
  inode_remove (inode);
  success = true;

 done:
  if (has_lock) write_release(&inode->rw);
  inode_close (inode);
  return success;
}

/* Reads the next directory entry in DIR and stores the name in
   NAME.  Returns true if successful, false if the directory
   contains no more entries. */
bool
dir_readdir (struct dir *dir, char name[NAME_MAX + 1])
{
  struct dir_entry e;

  while (inode_read_at (dir->inode, &e, sizeof e, dir->pos) == sizeof e) 
    {
      dir->pos += sizeof e;
      if (e.in_use)
        {
          strlcpy (name, e.name, NAME_MAX + 1);
          return true;
        } 
    }
  return false;
}
