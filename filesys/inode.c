#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"




//a block used to store sectors
struct indirection_block
  {
    int length;                         /* number of allocated sectors*/
    block_sector_t sectors[TABLE_SIZE];        /* array of sectors */
  };


int byte_to_i_block(off_t pos){
  return pos / (TABLE_SIZE * BLOCK_SECTOR_SIZE);
}

//frees and destroys block at sector
void ind_block_explode(block_sector_t sector){
  struct indirection_block *ind;
  ind = calloc (1, sizeof *ind);
  block_read(fs_device, sector, ind);
  int i;
  for(i = 0; i < ind->length; i++){
    free_map_release(ind->sectors[i], 1);
  }
  free_map_release(sector, 1);
  free(ind);
}

//adds a new sector to indirection block at sector
bool add_sector(block_sector_t sector){
  struct indirection_block *ind;
  ind = calloc (1, sizeof *ind);
  block_read(fs_device, sector, ind);
  ASSERT(ind->length <= TABLE_SIZE);
  ASSERT(ind->length != TABLE_SIZE);
  if(!free_map_allocate (1, &ind->sectors[ind->length])) {
    free(ind);
    return false;
  }
  ind->length++;
  static char zeros[BLOCK_SECTOR_SIZE];
  block_write(fs_device, sector, ind);
  block_write (fs_device, ind->sectors[ind->length-1], zeros);
  free(ind);
  return true;
}

//allocates sectors to fill indirection block at sector
bool fill_indirection_block(block_sector_t sector){
  int i;
  for(i = 0; i < TABLE_SIZE; i++){
    if(!add_sector(sector))
      return false;
  }
  return true;
}


void init_indirection_block(block_sector_t sector){
  struct indirection_block *ind;
  ind = calloc (1, sizeof *ind);
  block_write(fs_device, sector, ind);
  free(ind);
}


/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}



/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  ASSERT (inode != NULL);
  if (pos < inode->data.length){

    block_sector_t t = inode->data.indirection[pos / (TABLE_SIZE * BLOCK_SECTOR_SIZE)];
    struct indirection_block *table;
    table = calloc(1, sizeof *table);
    block_read(fs_device, t, table);

    int index = (pos / BLOCK_SECTOR_SIZE) % TABLE_SIZE;
    block_sector_t result = table->sectors[index];

    free(table);
    return result;
  }
  else {
    return -1;
  }
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
}

//grows inode by growth bytes
bool grow_inode(struct inode *inode, int growth){

  //growth within allocated sector
  if (bytes_to_sectors(inode->data.length) == bytes_to_sectors(inode->data.length + growth)) {
    inode->data.length += growth;
    block_write(fs_device, inode->sector, &inode->data);
    return true;
  }

  /* growth by more than one sector at a time is not supported */
  ASSERT(growth<=BLOCK_SECTOR_SIZE);

  //growth within allocated indirection block
  if(byte_to_i_block(inode->data.length - 1) == byte_to_i_block(inode->data.length + growth - 1)){
    add_sector(inode->data.indirection[byte_to_i_block(inode->data.length + growth - 1)]);
    inode->data.length += growth;
    block_write(fs_device, inode->sector, &inode->data);
    return true;
  }

  //grows in new indirection block

  if(!free_map_allocate(1, &inode->data.indirection[byte_to_i_block(inode->data.length + growth - 1)])){
    return false;
  }

  init_indirection_block(inode->data.indirection[byte_to_i_block(inode->data.length + growth - 1)]);
  add_sector(inode->data.indirection[byte_to_i_block(inode->data.length + growth - 1)]);
  inode->data.length += growth;
  block_write(fs_device, inode->sector, &inode->data);
  return true;      
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length, uint32_t is_dir)
{
  struct inode_disk *disk_inode = NULL;
  bool success = true;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      size_t sectors = bytes_to_sectors (length);                        
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
      disk_inode->is_dir = is_dir;
      int i;
      int num_tables = sectors / TABLE_SIZE;
      for (i=0; i<num_tables; i++) {
        if (!free_map_allocate (1, &disk_inode->indirection[i])) {
          free (disk_inode);
          return false;

        }

        init_indirection_block(disk_inode->indirection[i]);
        if(!fill_indirection_block(disk_inode->indirection[i])){
          free (disk_inode);
          return false;

        }
      }


      if (!free_map_allocate (1, &disk_inode->indirection[i])) {
        free (disk_inode);
        return false;
      }

      block_sector_t table = disk_inode->indirection[i];
      init_indirection_block(table);
      
      int sectors_left = sectors - (num_tables * TABLE_SIZE);

      for(i = 0; i< sectors_left; i++){
        if(!add_sector(table)){
          free (disk_inode);
          return false;
        }
      }

      block_write (fs_device, sector, disk_inode);

      success = true; 
    
      free (disk_inode);
    }

    
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  //if (!list_empty(&open_inodes))
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    } 
  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  init_readers_writers(&inode->rw);
  block_read (fs_device, inode->sector, &inode->data);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk. (Does it?  Check code.)
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
          int i;
          int limit = bytes_to_sectors(inode->data.length) / TABLE_SIZE;
          for (i= 0; i <= limit; i++) {
            ind_block_explode(inode->data.indirection[i]);
          }
          free_map_release (inode->sector, 1);
        }
      free (inode); 
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}


/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;


  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      if (sector_idx==-1)
        break;
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Read full sector directly into caller's buffer. */
          block_read (fs_device, sector_idx, buffer + bytes_read);
        }
      else 
        {
          /* Read sector into bounce buffer, then partially copy
             into caller's buffer. */
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }
          block_read (fs_device, sector_idx, bounce);
          memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
        }
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  free (bounce);
  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;

  if (inode->deny_write_cnt){
    return 0;
  }

  //file growth
  while(offset + size > inode->data.length){
    int min;
    min = offset + size - inode->data.length > BLOCK_SECTOR_SIZE ?
            BLOCK_SECTOR_SIZE :
            offset + size - inode->data.length;
    if(!grow_inode(inode, min)){
      return 0;
    }
  }

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;


      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      block_sector_t sector_idx = byte_to_sector (inode, offset);
      if (sector_idx==-1)
        break;


      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Write full sector directly to disk. */          
            block_write (fs_device, sector_idx, buffer + bytes_written);
          
        }
      else 
        {

          /* We need a bounce buffer. */
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }

          /* If the sector contains data before or after the chunk
             we're writing, then we need to read in the sector
             first.  Otherwise we start with a sector of all zeros. */
          if (sector_ofs > 0 || chunk_size < sector_left) 
            block_read (fs_device, sector_idx, bounce);
          else
            memset (bounce, 0, BLOCK_SECTOR_SIZE);
          memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
          block_write (fs_device, sector_idx, bounce);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  free (bounce);
  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  if(inode->data.is_dir)
    return;
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}
