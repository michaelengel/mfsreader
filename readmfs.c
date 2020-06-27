#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define BLOCKSIZE 512
#define ALLOCBLOCKSIZE 1024

uint16_t allocBM[1024] = { 0 };

typedef struct __attribute__((packed)) {
	uint16_t drSigWord;
	uint32_t drCrDate;
	uint32_t drLsBkUp;
	uint16_t drAtrb;
	uint16_t drNmFls;
	uint16_t drDirSt;
	uint16_t drBlLen;
	uint16_t drNmAlBlks;
	uint32_t drAlBlkSiz;
	uint32_t drClpSiz;
	uint16_t drAlBlSt;
	uint32_t drNxtFNum;
	uint16_t drFreeBks;
	uint8_t drVN;
	char drVName[255];
} mfs_vol_info;

typedef struct __attribute__((packed)) {
	uint8_t flFlags;	// bit 7 = 1 if entry used, bit 0 = 1 if file locks
	uint8_t flTyp;		// version number
	uint8_t flUsrWds[16];	// Finder info
	uint32_t flFlNum;	// file number
	uint16_t flStBlk;	// first allocaion block of data fork
	uint32_t flLgLen;	// logical end-of-file of data fork
	uint32_t flPyLen;	// physical end-of-file of data fork
	uint16_t flRStBlk;	// first allocaion block of resource fork
	uint32_t flRLgLen;	// logical end-of-file of resource fork
	uint32_t flRPyLen;	// physical end-of-file of resource fork
	uint32_t flCrDat;	// date and time of creation
	uint32_t flMdDat;	// date and time of modification
	uint8_t flNam;		// length of file name
	char flName[255];	// file name bytes
} mfs_dir_entry;

uint32_t b2l32(uint32_t num) {
	return ((num>>24)&0xff) | ((num<<8)&0xff0000) | ((num>>8)&0xff00) | ((num<<24)&0xff000000);
}

uint16_t b2l16(uint16_t num) {
	return ((num>>8)&0xff) | ((num<<8)&0xff00);
}

void writeFile(uint8_t *im, int fd, uint16_t nFirstAllocBlk, uint16_t firstBlock, uint32_t len) {
	int nextBlock = firstBlock;
	int blkn = nFirstAllocBlk * BLOCKSIZE;
	uint8_t *pos;

	while (nextBlock >= 2) {
		printf("Block %d\n", nextBlock);

		pos = im + blkn + ALLOCBLOCKSIZE * (nextBlock - 2);
		write(fd, pos, ALLOCBLOCKSIZE);

		nextBlock = allocBM[nextBlock];
	}
}

void readdir(uint8_t *im, uint16_t nFirstAllocBlk, uint16_t firstBlock, uint16_t nBlocks, uint16_t nFiles) {
	uint32_t pos;
	mfs_dir_entry *d;
	int n = 0;
	char fn[256];
	int fd;
	char exfn[256+5];

	pos = firstBlock * BLOCKSIZE;

	while (n < nFiles) {
		d = (mfs_dir_entry *)(im + pos);

		memcpy(fn, &(d->flName), d->flNam);
		fn[d->flNam] = '\0';

		printf("Pos: %d = 0x%08x used = %s\n", pos, pos, (d->flFlags & 0x80) ? "yes":"no");
		printf("File %02d length %d: >>%s<<\n", n, d->flNam, fn);
		printf("DATA fork: first block %d log eof %d phys eof %d\n", 
				b2l16(d->flStBlk), b2l32(d->flLgLen), b2l32(d->flPyLen));
		printf("RSRC fork: first block %d log eof %d phys eof %d\n", 
				b2l16(d->flRStBlk), b2l32(d->flRLgLen), b2l32(d->flRPyLen));
		printf("\n");

		pos = pos + 51 + ((d->flNam % 2) ? d->flNam : d->flNam+1);

		// write data and resource forks
		snprintf(exfn, 256+5, "%s.DATA", fn);
                printf("Writing %s\n", exfn);

		fd = open(exfn, O_CREAT|O_WRONLY, 0666);
		if (fd < 0) {
	                fprintf(stderr, "open(%s) failed\n", exfn);
			exit(1);
		}
		writeFile(im, fd, nFirstAllocBlk, b2l16(d->flStBlk), b2l32(d->flLgLen));
		close(fd);

		snprintf(exfn, 256+5, "%s.RSRC", fn);
                printf("Writing %s\n", exfn);

		fd = open(exfn, O_CREAT|O_WRONLY, 0666);
		if (fd < 0) {
	                fprintf(stderr, "open(%s) failed\n", exfn);
			exit(1);
		}
		writeFile(im, fd, nFirstAllocBlk, b2l16(d->flRStBlk), b2l32(d->flLgLen));
		close(fd);

		if (pos % BLOCKSIZE > BLOCKSIZE - 51) {
			printf("Skipping to next block\n");
			pos = (pos + BLOCKSIZE) / BLOCKSIZE * BLOCKSIZE;
		}
		n++;
	}
}

int main(int argc, char **argv) {

	if (argc < 2) {
		fprintf(stderr, "Usage: %s disk.img\n", argv[0]);
		exit(1);
	}

	struct stat st;
	if (stat(argv[1], &st) < 0) {
                fprintf(stderr, "stat(%s) failed\n", argv[1]);
                exit(1);
        }

	uint8_t *im = malloc(st.st_size);
	if (im == (uint8_t *)0) {
                fprintf(stderr, "malloc(%lld) failed\n", st.st_size);
                exit(1);
        }

	int fd;
	fd = open(argv[1], O_RDONLY);
	if (fd < 0) {
                fprintf(stderr, "open(%s) failed\n", argv[1]);
                exit(1);
        }

	size_t n;
	n = read(fd, im, st.st_size);
	if (n != st.st_size) {
                fprintf(stderr, "short read, expected %zu, got %lld bytes\n", n, st.st_size);
                exit(1);
        }
	close(fd);

	printf("Image %s loaded into memory\n", argv[1]);

	mfs_vol_info *vol;

	vol = (mfs_vol_info *)(im + 2 * BLOCKSIZE);

	if (vol->drSigWord != 0xd7d2) {
		printf("Volume signature invalid: %04x, expected 0xd2d7\n", vol->drSigWord);
	}
	
	char vn[256];
	memcpy(vn, vol->drVName, 256);
	vn[vol->drVN] = '\0';
	printf("Volume name: %s\n", vn);

	uint16_t nFiles;
	nFiles = b2l16(vol->drNmFls);
	printf("Number of files: %d\n", nFiles);

	uint16_t firstBlock;
	firstBlock = b2l16(vol->drDirSt);
	printf("First block of directory: %d\n", firstBlock);

	uint16_t nBlocks;
	nBlocks = b2l16(vol->drBlLen);
	printf("Length of dir: %d blocks\n", nBlocks);

	uint16_t nFirstAllocBlk;
	nFirstAllocBlk = b2l16(vol->drAlBlSt);
	printf("First alloc block: %d\n", nFirstAllocBlk);

	uint16_t nAllocBlks;
	nAllocBlks = b2l16(vol->drNmAlBlks);
	printf("Number of alloc blocks: %d\n", nAllocBlks);

	printf("Allocation bitmap at %08x:\n\n", 2 * BLOCKSIZE + 36 + vol->drVN + 1);
	uint8_t *bm;
	uint16_t blkn1;
	uint16_t blkn2;
	int nb = 2; // first block in alloc bitmap is 2

        bm = im + 2 * BLOCKSIZE + 64;

	while (nb < b2l16(vol->drNmAlBlks) + 2) {
		// 12 bit per entry!
		blkn1 = (*(bm+0) << 4) + ((*(bm+1) & 0xF0) >> 4);
		blkn2 = (*(bm+1) & 0x0F << 8) + *(bm+2);
		printf("%d %d ", blkn1, blkn2);
		bm += 3;	
		allocBM[nb] = blkn1;
		allocBM[nb+1] = blkn2;
		nb += 2;
	}

	printf("\n\n");

	readdir(im, nFirstAllocBlk, firstBlock, nBlocks, nFiles);
}
