//from github

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "disk.h"
#include "fs.h"
/* TODO: Phase 1 */
//need to put packed but was throwing error
#define FAT_EOC 0xFFFF

struct superblock
{
	char signature[8];	   // = “ECS150FS”; //name of file system
	uint16_t totalBlk;	   //total amount of blocks of virtual disk
	uint16_t rootDirIndex; //root directory block index
	uint16_t dataBlkStart; //data block start index
	uint16_t dataBlkNum;   //amount of data blocks
	uint8_t FATBlkNum;	   //number of blocks for FAT
	char unused[4079];	   //unused padding
};

struct rootDirectory
{
	char filename[16]; //set these values as constants later
	uint32_t fileSize;
	uint16_t firstDataBlk; //index of first data block
	uint8_t unused[10];	   //unused padding
};

struct fileDescriptor
{
	char filename[16];
	size_t offset;
};

//define global superblock
static struct superblock *sb;
typedef struct fileDescriptor fd;
//table which holds indexes in table
static fd fdTable[FS_OPEN_MAX_COUNT]; //when a file is opened, it will return a entry in the open file table
static uint16_t *fat_array;
static struct rootDirectory *rd_array;
int mount_flag = 0;
int fat_free_blk();
int rd_free_blk();
int next_fat_free_blk();
int allocate_new(int dbNum, int currBlkIndex);
int get_blk_num(int offset);
int offset_index(uint16_t firstDB, int offset);

int fs_mount(const char *diskname)
{
	assert(sizeof(struct superblock) == BLOCK_SIZE);

	if (block_disk_open(diskname) == -1)
	{
		return -1;
	}

	sb = malloc(BLOCK_SIZE);

	//allocation fail
	if (!sb)
	{
		return -1;
	}

	memset(sb, 0, BLOCK_SIZE);

	block_read(0, sb);

	//if sb name does not equal to ecs150fs
	char expectedSignature[8] = "ECS150FS";
	if (strncmp(sb->signature, expectedSignature, 8) != 0)
	{
		return -1;
	}
	if (2 + sb->FATBlkNum + sb->dataBlkNum != block_disk_count())
	{
		return -1;
	}
	//total block number = superblock + rootdirectory + FAT blocks + data blocks
	if ((2 + sb->FATBlkNum + sb->dataBlkNum) != sb->totalBlk)
	{
		return -1;
	}
	//root direct index = superblock + number of fat
	if (sb->rootDirIndex != 1 + sb->FATBlkNum)
	{
		return -1;
	}
	//data block is one index after root directory's index
	if (sb->dataBlkStart != sb->rootDirIndex + 1)
	{
		return -1;
	}

	//fat + file descriptor
	fat_array = malloc(sb->FATBlkNum * BLOCK_SIZE);
	if (!fat_array)
	{
		return -1;
	}
	for (int i = 0; i < sb->FATBlkNum; i++)
	{
		int checkFat = block_read(i + 1, &fat_array[i * (BLOCK_SIZE / 2)]);
		if (checkFat == -1)
		{
			return -1;
		}
	}

	rd_array = (struct rootDirectory *)malloc(sizeof(struct rootDirectory) * FS_FILE_MAX_COUNT);
	block_read(sb->rootDirIndex, rd_array);
	mount_flag = 1;
	return 0;
}

int fs_umount(void)
{
	if (block_write(0, sb) == -1)
	{
		return -1;
	}
	if (block_write(sb->rootDirIndex, rd_array) == -1)
	{
		return -1;
	}
	for (int i = 0; i < sb->FATBlkNum; i++)
	{
		if (block_write(i + 1, &fat_array[i * (BLOCK_SIZE / 2)]) == -1)
		{
			return -1;
		}
	}

	free(sb);
	free(fat_array);
	free(rd_array);
	return block_disk_close();
}

int fs_info(void)
{
	if (block_disk_count() == -1)
	{
		return -1;
	}
	printf("FS Info:\n");
	printf("total_blk_count=%d\n", sb->totalBlk);
	printf("fat_blk_count=%d\n", sb->FATBlkNum);
	printf("rdir_blk=%d\n", sb->rootDirIndex);
	printf("data_blk=%d\n", sb->dataBlkStart);
	printf("data_blk_count=%d\n", sb->dataBlkNum);
	int fatFree = fat_free_blk();
	int rdFree = rd_free_blk();
	printf("fat_free_ratio=%d/%d\n", fatFree, sb->dataBlkNum);
	printf("rdir_free_ratio=%d/%d\n", rdFree, FS_FILE_MAX_COUNT);
	return 0;
}

int fs_create(const char *filename)
{
	//Return -1 if no FS is currently mounted
	if (mount_flag == 0)
	{
		return -1;
	}
	//Return -1 if @filename is invalid(not sure about this one)
	if (filename[-1] != '\0')
	{
		return -1;
	}
	//Return -1 if string @filename is too long
	if (strlen(filename) >= FS_FILENAME_LEN)
	{
		return -1;
	}
	//find empty entry in root directory
	for (int i = 0; i < FS_FILE_MAX_COUNT; i++)
	{
		if (rd_array[i].filename[0] == '\0')
		{
			//specify the name
			strcpy(rd_array[i].filename, filename);
			//size = 0
			rd_array[i].fileSize = 0;
			//first index = FAT_EOC, have issue
			rd_array[i].firstDataBlk = FAT_EOC;
			//block_write(sb->rootDirIndex, rd_array);
			return 0;
		}
		else
		{
			//Return -1 if a file named @filename already exists
			if (strcmp(rd_array[i].filename, filename) == 0)
			{
				return -1;
			}
		}
	}
	//Return -1 if the root directory already contains %FS_FILE_MAX_COUNT files
	return -1;
}

int fs_delete(const char *filename)
{
	//Return -1 if no FS is currently mounted
	if (mount_flag == 0)
	{
		return -1;
	}
	//Return -1 if @filename is invalid
	if (filename[-1] != '\0')
	{
		return -1;
	}
	if (strlen(filename) >= FS_FILENAME_LEN)
	{
		return -1;
	}
	//Return -1 if file @filename is currently open
	for (int i = 0; i < FS_OPEN_MAX_COUNT; i++)
	{
		if (strcmp(fdTable[i].filename, filename) == 0)
		{
			return -1;
		}
	}
	for (int i = 0; i < FS_FILE_MAX_COUNT; i++)
	{
		if (strcmp(rd_array[i].filename, filename) == 0)
		{
			//free corresponding FAT if not an empty file
			if (rd_array[i].fileSize != 0)
			{
				size_t filesize = rd_array[i].fileSize;
				int blkNum = get_blk_num(filesize);
				uint16_t curr = rd_array[i].firstDataBlk;
				uint16_t next = 0;
				for(int i=0; i<blkNum; i++)
				{
					next = fat_array[curr];
					fat_array[curr] = 0;
					curr = next;
				}
			}
			//emptied entry
			rd_array[i].fileSize = 0;
			rd_array[i].firstDataBlk = 0;
			rd_array[i].filename[0] = '\0';
			//block_write(sb->rootDirIndex, rd_array);
			return 0;
		}
	}
	//Return -1 if there is no file named @filename to delete
	return -1;
}

int fs_ls(void)
{
	//Return -1 if no FS is currently mounted
	if (mount_flag == 0)
	{
		return -1;
	}
	printf("%s\n", "FS Ls:");
	//print all entries in root directory
	//format: file: hello.txt, size: 13, data_blk: 1
	for (int i = 0; i < FS_FILE_MAX_COUNT; i++)
	{
		if (rd_array[i].filename[0] != '\0')
		{
			printf("file: %s, ", rd_array[i].filename);
			printf("size: %d, ", rd_array[i].fileSize);
			printf("data_blk: %d\n", rd_array[i].firstDataBlk);
			//might have error for first data block when the data blk is empty
		}
	}
	return 0;
}

int fs_open(const char *filename)
{
	//Return -1 if no FS is currently mounted
	if (mount_flag == 0)
	{
		return -1;
	}
	//Return -1 if @filename is invalid
	if (filename[-1] != '\0')
	{
		return -1;
	}
	if (strlen(filename) >= FS_FILENAME_LEN)
	{
		return -1;
	}
	if (filename == NULL)
	{
		return -1;
	}
	//loop through all files
	for (int i = 0; i < FS_FILE_MAX_COUNT; i++)
	{
		if (strcmp(rd_array[i].filename, filename) == 0)
		{
			for (int j = 0; j < FS_OPEN_MAX_COUNT; j++)
			{
				if (fdTable[i].filename[0] == '\0')
				{
					strcpy(fdTable[i].filename, filename);
					fdTable[i].offset = 0;
					return i;
				}
			}
			//Return -1 if there are already %FS_OPEN_MAX_COUNT files currently open
			return -1;
		}
	}
	//Return -1 if there is no file named @filename to open
	return -1;
}

int fs_close(int fd)
{
	//Return -1 if no FS is currently mounted
	if (mount_flag == 0)
	{
		return -1;
	}
	//Return -1 if file descriptor @fd is out of bounds
	if (fd > FS_OPEN_MAX_COUNT)
	{
		return -1;
	}
	//Return -1 if file descriptor @fd is not currently open
	if (fdTable[fd].filename[0] == '\0')
	{
		return -1;
	}
	//might have error
	memset(fdTable[0].filename, '\0', sizeof(fdTable[0].filename));
	fdTable[fd].offset = 0;
	return 0;
}

int fs_stat(int fd)
{
	//Return -1 if no FS is currently mounted
	if (mount_flag == 0)
	{
		return -1;
	}
	//Return -1 if file descriptor @fd is out of bounds
	if (fd > FS_OPEN_MAX_COUNT)
	{
		return -1;
	}
	//Return -1 if file descriptor @fd is not currently open
	if (fdTable[fd].filename[0] == '\0')
	{
		return -1;
	}
	//Get the current size of the file pointed by file descriptor @fd
	for (int i = 0; i < FS_FILE_MAX_COUNT; i++)
	{
		if (strcmp(fdTable[fd].filename, rd_array[i].filename) == 0)
		{
			//return the size of the file corresponding to the specified file descriptor
			return rd_array[i].fileSize;
		}
	}
	return -1;
}

int fs_lseek(int fd, size_t offset)
{
	//Return -1 if no FS is currently mounted
	if (mount_flag == 0)
	{
		return -1;
	}
	//Return -1 if file descriptor @fd is invalid (i.e., out of bounds, or not currently open)
	if (fd > FS_OPEN_MAX_COUNT)
	{
		return -1;
	}
	if (fdTable[fd].filename[0] == '\0')
	{
		return -1;
	}
	//Return -1 if @offset is larger than the current file size. 0 otherwise.
	int curFileSize = 0;
	for (int i = 0; i < FS_FILE_MAX_COUNT; i++)
	{
		if (strcmp(rd_array[i].filename, fdTable[fd].filename) == 0)
		{
			curFileSize = rd_array[i].fileSize;
		}
	}
	if (offset > curFileSize)
	{
		return -1;
	}
	//find file by fd
	//set file offset as offset
	fdTable[fd].offset = offset;
	return 0;
}

int fs_write(int fd, void *buf, size_t count)
{
	//Return -1 if no FS is currently mounted
	if (mount_flag == 0)
	{
		return -1;
	}
	//Return -1 if file descriptor @fd is invalid (i.e., out of bounds, or not currently open)
	if (fd > FS_OPEN_MAX_COUNT)
	{
		return -1;
	}
	if (fdTable[fd].filename[0] == '\0')
	{
		return -1;
	}
	//Return -1 if @buf is NULL.
	if (buf == NULL)
	{
		return -1;
	}

	int rdIndex = 0;
	for (int i = 0; i < FS_FILE_MAX_COUNT; i++)
	{
		if (strcmp(fdTable[fd].filename, rd_array[i].filename) == 0)
		{
			rdIndex = i;
			continue;
		}
	}

	int firstDB = rd_array[rdIndex].firstDataBlk;
	int filesize = rd_array[rdIndex].fileSize;

	int currDBNum = get_blk_num(filesize);
	int startOffset = fdTable[fd].offset;
	int startDBNum = get_blk_num(startOffset);
	if(startOffset%BLOCK_SIZE != 0){
		startDBNum --;
	}
	int endOffset = startOffset + count;
	int endDBNum = get_blk_num(endOffset);
	//if block number being extended, then it's extending write, need to allocate new
	int allocatedBlkNum = 0;
	int blkNum = 0;
	int allocate_flag = 0;
	int currBlkIndex = 0;
	//need allocation
	if (endDBNum > currDBNum)
	{
		allocate_flag = 1;
		//if empty file
		int allocateBlkNum = endDBNum - currDBNum;
		//create new file
		if (firstDB == FAT_EOC)
		{
			firstDB = next_fat_free_blk();
			fat_array[firstDB] = FAT_EOC;
			currBlkIndex = firstDB;
			allocateBlkNum--;
			allocatedBlkNum++;
		}
		else
		{
			currBlkIndex = offset_index(firstDB, filesize);
		}
		allocatedBlkNum += allocate_new(allocateBlkNum, currBlkIndex);
	}
	else if (endDBNum < currDBNum)
	{
		blkNum = endDBNum - startDBNum;
	}

	if (blkNum == 0 && endOffset > startOffset)
	{
		blkNum = 1;
	}
	
	//check if sucessfully allocate and give index of ending block
	if (allocate_flag == 1 && allocatedBlkNum == endDBNum - currDBNum)
	{
		//successfully allocate that much
		blkNum = endDBNum - startDBNum;
	}
	else if (allocate_flag == 1 && allocatedBlkNum != endDBNum - currDBNum)
	{
		//did not have enough space to allocate
		blkNum = currDBNum + allocatedBlkNum - startDBNum;
		endOffset = blkNum * BLOCK_SIZE;
	}

	int Ret = endOffset - startOffset;

	int startBlkIndex = offset_index(firstDB, startOffset);
	uint16_t curr = startBlkIndex;
	int blkIndex = 0;
	int bufPointer = 0;
	int start = startOffset;
	int writeLen = Ret;
	int leftLen = 0;
	int cutOffset = endOffset - endOffset%BLOCK_SIZE;
	for (int i = 0; i < blkNum; i++)
	{
		if(i == blkNum-1){
		//last block
			writeLen = endOffset - start;
		}
		else{
			leftLen = cutOffset - (startDBNum + i + 1) * BLOCK_SIZE;
			writeLen = cutOffset - start - leftLen;
		}
		blkIndex = curr;
		curr = fat_array[curr];
		//buffer write
		if (writeLen < BLOCK_SIZE)
		{
			char *bounceBuf = malloc(BLOCK_SIZE);
			int checkRd = block_read(blkIndex + sb->dataBlkStart + 1, bounceBuf);
			memcpy(bounceBuf + start%BLOCK_SIZE, buf + bufPointer, writeLen);
			block_write(blkIndex + sb->dataBlkStart + 1, bounceBuf);
			free(bounceBuf);
		}
		//direct write
		else
		{
			block_write(blkIndex + sb->dataBlkStart + 1, buf + bufPointer);
		}
		bufPointer += writeLen;
		start += writeLen;
	}
	rd_array[rdIndex].firstDataBlk = firstDB;
	if(endOffset > filesize){
		rd_array[rdIndex].fileSize = endOffset;
	}
	fdTable[fd].offset = startOffset + endOffset;
	return Ret;
}

int fs_read(int fd, void *buf, size_t count)
{
	//Return -1 if no FS is currently mounted
	if (mount_flag == 0)
	{
		return -1;
	}
	//Return -1 if file descriptor @fd is invalid (i.e., out of bounds, or not currently open)
	if (fd > FS_OPEN_MAX_COUNT)
	{
		return -1;
	}
	if (fdTable[fd].filename[0] == '\0')
	{
		return -1;
	}
	//Return -1 if @buf is NULL.
	if (buf == NULL)
	{
		return -1;
	}

	//find read file's first data blk
	int firstDB = 0;
	int filesize = 0;
	for (int i = 0; i < FS_FILE_MAX_COUNT; i++)
	{
		if (strcmp(fdTable[fd].filename, rd_array[i].filename) == 0)
		{
			firstDB = rd_array[i].firstDataBlk;
			filesize = rd_array[i].fileSize;
			continue;
		}
	}

	//find start blk index
	int startOffset = fdTable[fd].offset;
	int startBlkIndex = offset_index(firstDB, startOffset);
	//find end blk index
	int endOffset = 0;
	if (filesize - startOffset <= count)
	{
		endOffset = filesize;
	}
	else
	{
		endOffset = startOffset + count;
	}

	int Ret = endOffset - startOffset;

	int blkNum = ceiling(endOffset) - ceiling(startOffset);
	if (blkNum == 0 && endOffset > startOffset)
	{
		blkNum = 1;
	}
	char *bounceBuf = malloc(BLOCK_SIZE * blkNum);
	//read from disk to buffer
	uint16_t curr = startBlkIndex;
	for (int i = 0; i < blkNum; i++)
	{
		int blkIndex = curr;
		curr = fat_array[curr];
		int checkRd = block_read(blkIndex + sb->dataBlkStart + 1, bounceBuf + BLOCK_SIZE * i);
		if (checkRd < 0)
		{
			return -1;
		}
	}
	memcpy(buf, bounceBuf + startOffset, endOffset - startOffset);
	fdTable[fd].offset = endOffset;
	free(bounceBuf);
	return Ret;
}


int fat_free_blk()
{
	int freeFat = 0;
	for (int i = 0; i < sb->dataBlkNum; i++)
	{
		if (fat_array[i] == 0)
		{
			freeFat = sb->dataBlkNum - i;
			return freeFat;
		}
	}
	return freeFat;
}

int rd_free_blk()
{
	int freeRd = 0;
	for (int i = 0; i < FS_FILE_MAX_COUNT; i++)
	{
		if (rd_array[i].filename[0] == '\0')
		{
			freeRd++;
		}
	}
	return freeRd;
}

int next_fat_free_blk()
{
	int freeFat = 0;
	for (int i = 0; i < sb->dataBlkNum; i++)
	{
		if (fat_array[i] == 0)
		{
			freeFat = i;
			return freeFat;
		}
	}
	return freeFat;
}

//need to rewrite allocate_new
int allocate_new(int dbNum, int currBlkIndex)
{
	int allocated = 0; //number of new blocks allocated already
	int curr = currBlkIndex;
	int next;
	for (int i = 0; i < dbNum; i++)
	{
		next = next_fat_free_blk();
		if (next == 0){
			//can't find next available block
			continue;
		}
		fat_array[curr] = next;
		fat_array[next] = FAT_EOC;
		curr = next;
		allocated++;
	}
	return allocated;
}

//allocates a new data block and link it at the end of the file’s data block chain
int get_blk_num(int offset)
{
	int ret = offset / BLOCK_SIZE;
	if (ret * BLOCK_SIZE < offset)
	{
		ret += 1;
	}
	return ret;
}

//returns the index of the data block corresponding to the file’s offset
int offset_index(uint16_t firstDB, int offset)
{
	int blkNum = get_blk_num(offset);
	uint16_t curr = firstDB;
	int blkIndex = firstDB;
	for (int i = 0; i < blkNum; i++)
	{
		blkIndex = curr;
		curr = fat_array[curr];
	}
	return blkIndex;
}
