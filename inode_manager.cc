#include "inode_manager.h"
#include <time.h>

// disk layer -----------------------------------------

disk::disk()
{
  bzero(blocks, sizeof(blocks));
}

void
disk::read_block(blockid_t id, char *buf)
{
  if (((id >> 31) != 0) || id > BLOCK_NUM || buf == NULL) {
    return;
  }
  
  memcpy(buf, blocks[id], BLOCK_SIZE);

  return;
}

void
disk::write_block(blockid_t id, const char *buf)
{
  if (((id >> 31) != 0) || id > BLOCK_NUM || buf == NULL) {
    return;
  }

  memcpy(blocks[id], buf, BLOCK_SIZE);

  return;
}

// block layer -----------------------------------------

// Allocate a free disk block.
blockid_t
block_manager::alloc_block()
{
  /*
   * your code goes here.
   * note: you should mark the corresponding bit in block bitmap when alloc.
   * you need to think about which block you can start to be allocated.
   */
  blockid_t firstBlock = IBLOCK(sb.ninodes, sb.nblocks) + 1; // start with the first block
  char bitmapBuf[BLOCK_SIZE];

  for (uint32_t i = firstBlock; i < sb.nblocks; i += BPB) {
    read_block(BBLOCK(i), bitmapBuf);
    
    // take care of bytemap in block
    for (int j = 0; j < BLOCK_SIZE; j++) {
      char temp = bitmapBuf[j];

      // take care of bitmap in every byte
      for (int k = 0; k < 8; k++) {
        // change the bit to 1
        if ((temp & (0x1 << k)) == 0) {
          bitmapBuf[j] |= (0x1 << k);
          write_block(BBLOCK(i), bitmapBuf);

          return i + k + 8 * j;
        }
      }
    }
  }

  // exit if no block available;
  exit(0);
}

void
block_manager::free_block(uint32_t id)
{
  /* 
   * your code goes here.
   * note: you should unmark the corresponding bit in the block bitmap when free.
   */
  char temp[BLOCK_SIZE];
  read_block(BBLOCK(id), temp); // read the block containing the bitmap
  int index = (id % BPB) / 8; // get the index of bit in bitmap block
  char mask = ~(0x1 << ((id % BPB) % 8)); // set a mask where all bits are 1 except the freeing bit
  temp[index] = temp[index] & mask; // change freeing bit
  write_block(BBLOCK(id), temp);

  return;
}

// The layout of disk should be like this:
// |<-sb->|<-free block bitmap->|<-inode table->|<-data->|
block_manager::block_manager()
{
  d = new disk();

  // format the disk
  sb.size = BLOCK_SIZE * BLOCK_NUM;
  sb.nblocks = BLOCK_NUM;
  sb.ninodes = INODE_NUM;

}

void
block_manager::read_block(uint32_t id, char *buf)
{
  d->read_block(id, buf);
}

void
block_manager::write_block(uint32_t id, const char *buf)
{
  d->write_block(id, buf);
}

// inode layer -----------------------------------------

inode_manager::inode_manager()
{
  bm = new block_manager();
  uint32_t root_dir = alloc_inode(extent_protocol::T_DIR);
  if (root_dir != 1) {
    printf("\tim: error! alloc first inode %d, should be 1\n", root_dir);
    exit(0);
  }
}

/* Create a new file.
 * Return its inum. */
uint32_t
inode_manager::alloc_inode(uint32_t type)
{
  /* 
   * your code goes here.
   * note: the normal inode block should begin from the 2nd inode block.
   * the 1st is used for root_dir, see inode_manager::inode_manager().
   */
  for (uint32_t i = 1; i < bm->sb.ninodes; i++) {
    struct inode* tempinode = get_inode(i);

    // if get_inode returns NULL, means the inode not used
    if (tempinode == NULL) {
      struct inode newInode;
      newInode.size = 0;
      newInode.type = type;
      newInode.mtime = time(NULL);
      newInode.atime = time(NULL);
      newInode.ctime = time(NULL);
      put_inode(i, &newInode);
      
      return i;
    } else {
      free(tempinode);
    }
  }
  
  return 1;
}

void
inode_manager::free_inode(uint32_t inum)
{
  /* 
   * your code goes here.
   * note: you need to check if the inode is already a freed one;
   * if not, clear it, and remember to write back to disk.
   */
  if (inum < 0 || inum >= INODE_NUM) {
    return;
  }

  // check if freed
  struct inode *inode = get_inode(inum);
  if (inode == NULL) {
    return;
  }

  memset(inode, 0, sizeof(struct inode));
  put_inode(inum, inode);
  free(inode);

  return;
}


/* Return an inode structure by inum, NULL otherwise.
 * Caller should release the memory. */
struct inode* 
inode_manager::get_inode(uint32_t inum)
{
  struct inode *ino, *ino_disk;
  char buf[BLOCK_SIZE];

  printf("\tim: get_inode %d\n", inum);

  if (inum < 0 || inum >= INODE_NUM) {
    printf("\tim: inum out of range\n");
    return NULL;
  }

  bm->read_block(IBLOCK(inum, bm->sb.nblocks), buf);
  // printf("%s:%d\n", __FILE__, __LINE__);

  ino_disk = (struct inode*)buf + inum%IPB;
  if (ino_disk->type == 0) {
    printf("\tim: inode not exist\n");
    return NULL;
  }

  ino = (struct inode*)malloc(sizeof(struct inode));
  *ino = *ino_disk;

  return ino;
}

void
inode_manager::put_inode(uint32_t inum, struct inode *ino)
{
  char buf[BLOCK_SIZE];
  struct inode *ino_disk;

  printf("\tim: put_inode %d\n", inum);
  if (ino == NULL)
    return;

  bm->read_block(IBLOCK(inum, bm->sb.nblocks), buf);
  ino_disk = (struct inode*)buf + inum%IPB;
  *ino_disk = *ino;
  bm->write_block(IBLOCK(inum, bm->sb.nblocks), buf);
}

#define MIN(a,b) ((a)<(b) ? (a) : (b))

/* Get all the data of a file by inum. 
 * Return alloced data, should be freed by caller. */
void
inode_manager::read_file(uint32_t inum, char **buf_out, int *size)
{
  /*
   * your code goes here.
   * note: read blocks related to inode number inum,
   * and copy them to buf_Out
   */
  if (inum < 0 || inum >= INODE_NUM) {
    return;
  }

  struct inode *inode = get_inode(inum);
  *size = inode->size;

  int i;
  int requiredBlockNumber = (*size % BLOCK_SIZE == 0) ? *size / BLOCK_SIZE : *size /BLOCK_SIZE + 1;
  *buf_out = (char *)malloc(requiredBlockNumber * BLOCK_SIZE);

  for (i = 0; i < MIN(NDIRECT, requiredBlockNumber); i++) {
    blockid_t id = inode->blocks[i];
    bm->read_block(id, i * BLOCK_SIZE + *buf_out);
  }

  if (i < requiredBlockNumber) {
    blockid_t extraBlocks[NINDIRECT];
    bm->read_block(inode->blocks[NDIRECT], (char *)extraBlocks);

    for (; i < requiredBlockNumber; i++) {
      blockid_t id = extraBlocks[i - NDIRECT];
      bm->read_block(id, i * BLOCK_SIZE + *buf_out);
    }
  }

  inode->atime = time(NULL);
  put_inode(inum, inode);
  free(inode);

  return;
}

/* alloc/free blocks if needed */
void
inode_manager::write_file(uint32_t inum, const char *buf, int size)
{
  /*
   * your code goes here.
   * note: write buf to blocks of inode inum.
   * you need to consider the situation when the size of buf 
   * is larger or smaller than the size of original inode
   */
  // first check size
  if (size < 0 || (unsigned int)size > BLOCK_SIZE * MAXFILE) {
    return;
  }

  struct inode *newInode = get_inode(inum);
  int requiredBlockNumber = (size % BLOCK_SIZE == 0) ? size / BLOCK_SIZE : size /BLOCK_SIZE + 1;
  int inodeMetaSizeTemp = newInode -> size;
  int providedBlockNumber = (inodeMetaSizeTemp % BLOCK_SIZE == 0) ? inodeMetaSizeTemp / BLOCK_SIZE : inodeMetaSizeTemp / BLOCK_SIZE + 1;
  char tempBuf[BLOCK_SIZE];

  // deals with metadata stated size > request size
  // need to free blocks
  if (providedBlockNumber > requiredBlockNumber) {
    if (requiredBlockNumber <= NDIRECT) {
      if (providedBlockNumber <= NDIRECT) {
        for (int i = requiredBlockNumber; i < providedBlockNumber; i++) {
          bm->free_block(newInode->blocks[i]);
        }
      } else {
        bm->read_block(newInode->blocks[NDIRECT], tempBuf);
        blockid_t *blockArray = (blockid_t *)tempBuf;

        for (int i = 0; i < providedBlockNumber - NDIRECT; i++) {
          bm->free_block(blockArray[i]);
        }
        for (int i = requiredBlockNumber; i < NDIRECT; i++) {
          bm->free_block(newInode->blocks[i]);
        }
      }
    } else { // requiredBlockNumber > NDIRECT
      bm->read_block(newInode->blocks[NDIRECT], tempBuf);
      blockid_t *blockArray = (blockid_t *)tempBuf;

      for (int i = (requiredBlockNumber - NDIRECT); i < (providedBlockNumber - NDIRECT); i++) {
        bm->free_block(blockArray[i]);
      }
    }
  } else { // providedBlockNumber < requiredBlockNumber, need to allocate new blocka
    if (requiredBlockNumber <= NDIRECT) {
      // just use the inode block as it is. No new inode block needs to be allocated
      for (int i = providedBlockNumber; i < requiredBlockNumber; i++) {
        newInode->blocks[i] = bm->alloc_block();
      }
    } else { // requiredBlockNumber > NDIRECT
      if (providedBlockNumber <= NDIRECT) {
        for (int i = providedBlockNumber; i <= NDIRECT; i++) {
          newInode->blocks[i] = bm->alloc_block();
        }
        blockid_t blocks[NINDIRECT];
        for (int i = 0; i < requiredBlockNumber - NDIRECT; i++) {
          blocks[i] = bm->alloc_block();
        }
        bm->write_block(newInode->blocks[NDIRECT], (char *)blocks);
      } else { // providedBlockNumber > NDIRECT
        bm->read_block(newInode->blocks[NDIRECT], tempBuf);
        blockid_t *blocks = (blockid_t *)tempBuf;
        for (int i = providedBlockNumber - NDIRECT; i < (requiredBlockNumber - NDIRECT); i++) {
          blocks[i] = bm->alloc_block();
        }
        bm->write_block(newInode->blocks[NDIRECT], (char *)blocks);
      }
    }
  }

  // after allocate or deallocate blocks, write files
  // int i = 0;
  // char writeBuf[BLOCK_SIZE];

  // for (; i < MIN(requiredBlockNumber, NDIRECT); i++) {
  //   blockid_t id = newInode->blocks[i];
  //   memset(writeBuf, 0, BLOCK_SIZE);
  //   int length = MIN(BLOCK_SIZE, size - i * BLOCK_SIZE);
  //   memcpy(writeBuf, buf + i * BLOCK_SIZE, length);
  //   bm->write_block(id, writeBuf);
  // }

  // for (; i < requiredBlockNumber; i++) {
  //   blockid_t id = tempBuf[i - NDIRECT];
  //   memset(writeBuf, 0, BLOCK_SIZE);
  //   int length = MIN(BLOCK_SIZE, size - i * BLOCK_SIZE);
  //   memcpy(writeBuf, buf + i * BLOCK_SIZE, length);
  //   bm->write_block(id, writeBuf);
  // }

  // newInode->size = size;
  // newInode->ctime = newInode->mtime = newInode->atime = time(NULL);
  // put_inode(inum, newInode);
  // free(newInode);

  // return;

  int r = size;
  
  if (requiredBlockNumber <= NDIRECT) {
    for (int i = 0; i < requiredBlockNumber; i++) {
      bm->write_block(newInode->blocks[i], i * BLOCK_SIZE + buf);     
    }
  } else {
    for (int i = 0; i < NDIRECT; i++) {
      bm->write_block(newInode->blocks[i], i * BLOCK_SIZE + buf);     
    }
    blockid_t temp[NINDIRECT];
    bm->read_block(newInode->blocks[NDIRECT], (char *)temp);
    r = size - NDIRECT*BLOCK_SIZE;
    int i = 0;

    while (r > 0) {
      if (r >= BLOCK_SIZE) {
        bm->write_block(temp[i], buf + (i + NDIRECT) * BLOCK_SIZE);
      } else {
        bzero(tempBuf, sizeof(tempBuf));
        memcpy(tempBuf, buf + (i + NDIRECT) * BLOCK_SIZE, r);
        bm->write_block(temp[i], tempBuf);
      }

      i++;
      r -= BLOCK_SIZE;
    }
  }
  
  // update the inode
  newInode->size = size;
  newInode->ctime = newInode->mtime = time(NULL);
  put_inode(inum, newInode);
  free(newInode);

  return;
}

void
inode_manager::getattr(uint32_t inum, extent_protocol::attr &a)
{
  /*
   * your code goes here.
   * note: get the attributes of inode inum.
   * you can refer to "struct attr" in extent_protocol.h
   */
  struct inode* tempInode = get_inode(inum);

  if (tempInode != NULL) {
    a.atime = tempInode->atime;
    a.ctime = tempInode->ctime;
    a.mtime = tempInode->mtime;
    a.size = tempInode->size;
    a.type = tempInode->type;
    free(tempInode);
  }

  return;
}

void
inode_manager::remove_file(uint32_t inum)
{
  /*
   * your code goes here
   * note: you need to consider about both the data block and inode of the file
   */
  struct inode* inode = get_inode(inum);
  char temp[BLOCK_SIZE];
  int size = inode->size;
  int numberOfBlocks = (size % BLOCK_SIZE == 0) ? size / BLOCK_SIZE : size / BLOCK_SIZE + 1;

  if (numberOfBlocks <= NDIRECT) {
    for (int i = 0; i < numberOfBlocks; i++) {
      bm->free_block(inode->blocks[i]);
    }
  } else {
    for (int i = 0; i < NDIRECT; i++) {
      bm->free_block(inode->blocks[i]);
    }
    bm->read_block(inode->blocks[NDIRECT], temp);
    blockid_t *blocks = (blockid_t *)temp;

    for (int i = 0; i < numberOfBlocks - NDIRECT; i++) {
      bm->free_block(blocks[i]);
    }
    bm->free_block(inode->blocks[NDIRECT]);
  }

  free_inode(inum);
  free(inode);

  return;
}
