/*
 * Linux driver for NAND Flash Translation Layer
 *
 * Copyright © 1999 Machine Vision Holdings, Inc.
 * Copyright © 1999-2010 David Woodhouse <dwmw2@infradead.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#define PRERELEASE

#include <linux/kernel.h>
#include <linux/module.h>
#include <asm/errno.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/hdreg.h>
#include <linux/blkdev.h>

#include <linux/kmod.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/nftl.h>
#include <linux/mtd/blktrans.h>


#define MAX_LOOPS 10000


static void nftl_add_mtd(struct mtd_blktrans_ops *tr, struct mtd_info *mtd)
{
	struct NFTLrecord *nftl;
	unsigned long temp;

	if (mtd->type != MTD_NANDFLASH || mtd->size > UINT_MAX)
		return;
	
	if (memcmp(mtd->name, "DiskOnChip", 10))
		return;

	pr_debug("NFTL: add_mtd for %s\n", mtd->name);

	nftl = kzalloc(sizeof(struct NFTLrecord), GFP_KERNEL);

	if (!nftl)
		return;

	nftl->mbd.mtd = mtd;
	nftl->mbd.devnum = -1;

	nftl->mbd.tr = tr;

        if (NFTL_mount(nftl) < 0) {
		printk(KERN_WARNING "NFTL: could not mount device\n");
		kfree(nftl);
		return;
        }

	

	
	nftl->cylinders = 1024;
	nftl->heads = 16;

	temp = nftl->cylinders * nftl->heads;
	nftl->sectors = nftl->mbd.size / temp;
	if (nftl->mbd.size % temp) {
		nftl->sectors++;
		temp = nftl->cylinders * nftl->sectors;
		nftl->heads = nftl->mbd.size / temp;

		if (nftl->mbd.size % temp) {
			nftl->heads++;
			temp = nftl->heads * nftl->sectors;
			nftl->cylinders = nftl->mbd.size / temp;
		}
	}

	if (nftl->mbd.size != nftl->heads * nftl->cylinders * nftl->sectors) {
		printk(KERN_WARNING "NFTL: cannot calculate a geometry to "
		       "match size of 0x%lx.\n", nftl->mbd.size);
		printk(KERN_WARNING "NFTL: using C:%d H:%d S:%d "
			"(== 0x%lx sects)\n",
			nftl->cylinders, nftl->heads , nftl->sectors,
			(long)nftl->cylinders * (long)nftl->heads *
			(long)nftl->sectors );
	}

	if (add_mtd_blktrans_dev(&nftl->mbd)) {
		kfree(nftl->ReplUnitTable);
		kfree(nftl->EUNtable);
		kfree(nftl);
		return;
	}
#ifdef PSYCHO_DEBUG
	printk(KERN_INFO "NFTL: Found new nftl%c\n", nftl->mbd.devnum + 'a');
#endif
}

static void nftl_remove_dev(struct mtd_blktrans_dev *dev)
{
	struct NFTLrecord *nftl = (void *)dev;

	pr_debug("NFTL: remove_dev (i=%d)\n", dev->devnum);

	del_mtd_blktrans_dev(dev);
	kfree(nftl->ReplUnitTable);
	kfree(nftl->EUNtable);
}

int nftl_read_oob(struct mtd_info *mtd, loff_t offs, size_t len,
		  size_t *retlen, uint8_t *buf)
{
	loff_t mask = mtd->writesize - 1;
	struct mtd_oob_ops ops;
	int res;

	ops.mode = MTD_OPS_PLACE_OOB;
	ops.ooboffs = offs & mask;
	ops.ooblen = len;
	ops.oobbuf = buf;
	ops.datbuf = NULL;

	res = mtd_read_oob(mtd, offs & ~mask, &ops);
	*retlen = ops.oobretlen;
	return res;
}

int nftl_write_oob(struct mtd_info *mtd, loff_t offs, size_t len,
		   size_t *retlen, uint8_t *buf)
{
	loff_t mask = mtd->writesize - 1;
	struct mtd_oob_ops ops;
	int res;

	ops.mode = MTD_OPS_PLACE_OOB;
	ops.ooboffs = offs & mask;
	ops.ooblen = len;
	ops.oobbuf = buf;
	ops.datbuf = NULL;

	res = mtd_write_oob(mtd, offs & ~mask, &ops);
	*retlen = ops.oobretlen;
	return res;
}

#ifdef CONFIG_NFTL_RW

static int nftl_write(struct mtd_info *mtd, loff_t offs, size_t len,
		      size_t *retlen, uint8_t *buf, uint8_t *oob)
{
	loff_t mask = mtd->writesize - 1;
	struct mtd_oob_ops ops;
	int res;

	ops.mode = MTD_OPS_PLACE_OOB;
	ops.ooboffs = offs & mask;
	ops.ooblen = mtd->oobsize;
	ops.oobbuf = oob;
	ops.datbuf = buf;
	ops.len = len;

	res = mtd_write_oob(mtd, offs & ~mask, &ops);
	*retlen = ops.retlen;
	return res;
}

static u16 NFTL_findfreeblock(struct NFTLrecord *nftl, int desperate )
{
	u16 pot = nftl->LastFreeEUN;
	int silly = nftl->nb_blocks;

	
	if (!desperate && nftl->numfreeEUNs < 2) {
		pr_debug("NFTL_findfreeblock: there are too few free EUNs\n");
		return BLOCK_NIL;
	}

	
	do {
		if (nftl->ReplUnitTable[pot] == BLOCK_FREE) {
			nftl->LastFreeEUN = pot;
			nftl->numfreeEUNs--;
			return pot;
		}

		if (++pot > nftl->lastEUN)
			pot = le16_to_cpu(nftl->MediaHdr.FirstPhysicalEUN);

		if (!silly--) {
			printk("Argh! No free blocks found! LastFreeEUN = %d, "
			       "FirstEUN = %d\n", nftl->LastFreeEUN,
			       le16_to_cpu(nftl->MediaHdr.FirstPhysicalEUN));
			return BLOCK_NIL;
		}
	} while (pot != nftl->LastFreeEUN);

	return BLOCK_NIL;
}

static u16 NFTL_foldchain (struct NFTLrecord *nftl, unsigned thisVUC, unsigned pendingblock )
{
	struct mtd_info *mtd = nftl->mbd.mtd;
	u16 BlockMap[MAX_SECTORS_PER_UNIT];
	unsigned char BlockLastState[MAX_SECTORS_PER_UNIT];
	unsigned char BlockFreeFound[MAX_SECTORS_PER_UNIT];
	unsigned int thisEUN;
	int block;
	int silly;
	unsigned int targetEUN;
	struct nftl_oob oob;
	int inplace = 1;
	size_t retlen;

	memset(BlockMap, 0xff, sizeof(BlockMap));
	memset(BlockFreeFound, 0, sizeof(BlockFreeFound));

	thisEUN = nftl->EUNtable[thisVUC];

	if (thisEUN == BLOCK_NIL) {
		printk(KERN_WARNING "Trying to fold non-existent "
		       "Virtual Unit Chain %d!\n", thisVUC);
		return BLOCK_NIL;
	}

	silly = MAX_LOOPS;
	targetEUN = BLOCK_NIL;
	while (thisEUN <= nftl->lastEUN ) {
		unsigned int status, foldmark;

		targetEUN = thisEUN;
		for (block = 0; block < nftl->EraseSize / 512; block ++) {
			nftl_read_oob(mtd, (thisEUN * nftl->EraseSize) +
				      (block * 512), 16 , &retlen,
				      (char *)&oob);
			if (block == 2) {
				foldmark = oob.u.c.FoldMark | oob.u.c.FoldMark1;
				if (foldmark == FOLD_MARK_IN_PROGRESS) {
					pr_debug("Write Inhibited on EUN %d\n", thisEUN);
					inplace = 0;
				} else {
					inplace = 1;
				}
			}
			status = oob.b.Status | oob.b.Status1;
			BlockLastState[block] = status;

			switch(status) {
			case SECTOR_FREE:
				BlockFreeFound[block] = 1;
				break;

			case SECTOR_USED:
				if (!BlockFreeFound[block])
					BlockMap[block] = thisEUN;
				else
					printk(KERN_WARNING
					       "SECTOR_USED found after SECTOR_FREE "
					       "in Virtual Unit Chain %d for block %d\n",
					       thisVUC, block);
				break;
			case SECTOR_DELETED:
				if (!BlockFreeFound[block])
					BlockMap[block] = BLOCK_NIL;
				else
					printk(KERN_WARNING
					       "SECTOR_DELETED found after SECTOR_FREE "
					       "in Virtual Unit Chain %d for block %d\n",
					       thisVUC, block);
				break;

			case SECTOR_IGNORE:
				break;
			default:
				printk("Unknown status for block %d in EUN %d: %x\n",
				       block, thisEUN, status);
			}
		}

		if (!silly--) {
			printk(KERN_WARNING "Infinite loop in Virtual Unit Chain 0x%x\n",
			       thisVUC);
			return BLOCK_NIL;
		}

		thisEUN = nftl->ReplUnitTable[thisEUN];
	}

	if (inplace) {
		for (block = 0; block < nftl->EraseSize / 512 ; block++) {
			if (BlockLastState[block] != SECTOR_FREE &&
			    BlockMap[block] != BLOCK_NIL &&
			    BlockMap[block] != targetEUN) {
				pr_debug("Setting inplace to 0. VUC %d, "
				      "block %d was %x lastEUN, "
				      "and is in EUN %d (%s) %d\n",
				      thisVUC, block, BlockLastState[block],
				      BlockMap[block],
				      BlockMap[block]== targetEUN ? "==" : "!=",
				      targetEUN);
				inplace = 0;
				break;
			}
		}

		if (pendingblock >= (thisVUC * (nftl->EraseSize / 512)) &&
		    pendingblock < ((thisVUC + 1)* (nftl->EraseSize / 512)) &&
		    BlockLastState[pendingblock - (thisVUC * (nftl->EraseSize / 512))] !=
		    SECTOR_FREE) {
			pr_debug("Pending write not free in EUN %d. "
			      "Folding out of place.\n", targetEUN);
			inplace = 0;
		}
	}

	if (!inplace) {
		pr_debug("Cannot fold Virtual Unit Chain %d in place. "
		      "Trying out-of-place\n", thisVUC);
		
		targetEUN = NFTL_findfreeblock(nftl, 1);
		if (targetEUN == BLOCK_NIL) {
			printk(KERN_WARNING
			       "NFTL_findfreeblock(desperate) returns 0xffff.\n");
			return BLOCK_NIL;
		}
	} else {
		oob.u.c.FoldMark = oob.u.c.FoldMark1 = cpu_to_le16(FOLD_MARK_IN_PROGRESS);
		oob.u.c.unused = 0xffffffff;
		nftl_write_oob(mtd, (nftl->EraseSize * targetEUN) + 2 * 512 + 8,
			       8, &retlen, (char *)&oob.u);
	}

	pr_debug("Folding chain %d into unit %d\n", thisVUC, targetEUN);
	for (block = 0; block < nftl->EraseSize / 512 ; block++) {
		unsigned char movebuf[512];
		int ret;

		
		if (BlockMap[block] == targetEUN ||
		    (pendingblock == (thisVUC * (nftl->EraseSize / 512) + block))) {
			continue;
		}

		if (BlockMap[block] == BLOCK_NIL)
			continue;

		ret = mtd_read(mtd,
			       (nftl->EraseSize * BlockMap[block]) + (block * 512),
			       512,
			       &retlen,
			       movebuf);
		if (ret < 0 && !mtd_is_bitflip(ret)) {
			ret = mtd_read(mtd,
				       (nftl->EraseSize * BlockMap[block]) + (block * 512),
				       512,
				       &retlen,
				       movebuf);
			if (ret != -EIO)
				printk("Error went away on retry.\n");
		}
		memset(&oob, 0xff, sizeof(struct nftl_oob));
		oob.b.Status = oob.b.Status1 = SECTOR_USED;

		nftl_write(nftl->mbd.mtd, (nftl->EraseSize * targetEUN) +
			   (block * 512), 512, &retlen, movebuf, (char *)&oob);
	}

	
	oob.u.a.VirtUnitNum = oob.u.a.SpareVirtUnitNum = cpu_to_le16(thisVUC);
	oob.u.a.ReplUnitNum = oob.u.a.SpareReplUnitNum = BLOCK_NIL;

	nftl_write_oob(mtd, (nftl->EraseSize * targetEUN) + 8,
		       8, &retlen, (char *)&oob.u);

	

	thisEUN = nftl->EUNtable[thisVUC];
	pr_debug("Want to erase\n");

	while (thisEUN <= nftl->lastEUN && thisEUN != targetEUN) {
		unsigned int EUNtmp;

		EUNtmp = nftl->ReplUnitTable[thisEUN];

		if (NFTL_formatblock(nftl, thisEUN) < 0) {
			nftl->ReplUnitTable[thisEUN] = BLOCK_RESERVED;
		} else {
			
			nftl->ReplUnitTable[thisEUN] = BLOCK_FREE;
			nftl->numfreeEUNs++;
		}
		thisEUN = EUNtmp;
	}

	
	nftl->ReplUnitTable[targetEUN] = BLOCK_NIL;
	nftl->EUNtable[thisVUC] = targetEUN;

	return targetEUN;
}

static u16 NFTL_makefreeblock( struct NFTLrecord *nftl , unsigned pendingblock)
{
	u16 LongestChain = 0;
	u16 ChainLength = 0, thislen;
	u16 chain, EUN;

	for (chain = 0; chain < le32_to_cpu(nftl->MediaHdr.FormattedSize) / nftl->EraseSize; chain++) {
		EUN = nftl->EUNtable[chain];
		thislen = 0;

		while (EUN <= nftl->lastEUN) {
			thislen++;
			
			EUN = nftl->ReplUnitTable[EUN] & 0x7fff;
			if (thislen > 0xff00) {
				printk("Endless loop in Virtual Chain %d: Unit %x\n",
				       chain, EUN);
			}
			if (thislen > 0xff10) {
				thislen = 0;
				break;
			}
		}

		if (thislen > ChainLength) {
			
			ChainLength = thislen;
			LongestChain = chain;
		}
	}

	if (ChainLength < 2) {
		printk(KERN_WARNING "No Virtual Unit Chains available for folding. "
		       "Failing request\n");
		return BLOCK_NIL;
	}

	return NFTL_foldchain (nftl, LongestChain, pendingblock);
}

static inline u16 NFTL_findwriteunit(struct NFTLrecord *nftl, unsigned block)
{
	u16 lastEUN;
	u16 thisVUC = block / (nftl->EraseSize / 512);
	struct mtd_info *mtd = nftl->mbd.mtd;
	unsigned int writeEUN;
	unsigned long blockofs = (block * 512) & (nftl->EraseSize -1);
	size_t retlen;
	int silly, silly2 = 3;
	struct nftl_oob oob;

	do {

		lastEUN = BLOCK_NIL;
		writeEUN = nftl->EUNtable[thisVUC];
		silly = MAX_LOOPS;
		while (writeEUN <= nftl->lastEUN) {
			struct nftl_bci bci;
			size_t retlen;
			unsigned int status;

			lastEUN = writeEUN;

			nftl_read_oob(mtd,
				      (writeEUN * nftl->EraseSize) + blockofs,
				      8, &retlen, (char *)&bci);

			pr_debug("Status of block %d in EUN %d is %x\n",
			      block , writeEUN, le16_to_cpu(bci.Status));

			status = bci.Status | bci.Status1;
			switch(status) {
			case SECTOR_FREE:
				return writeEUN;

			case SECTOR_DELETED:
			case SECTOR_USED:
			case SECTOR_IGNORE:
				break;
			default:
				
				break;
			}

			if (!silly--) {
				printk(KERN_WARNING
				       "Infinite loop in Virtual Unit Chain 0x%x\n",
				       thisVUC);
				return BLOCK_NIL;
			}

			
			writeEUN = nftl->ReplUnitTable[writeEUN];
		}


		
		writeEUN = NFTL_findfreeblock(nftl, 0);

		if (writeEUN == BLOCK_NIL) {

			
			

			
			writeEUN = NFTL_makefreeblock(nftl, BLOCK_NIL);

			if (writeEUN == BLOCK_NIL) {
				pr_debug("Using desperate==1 to find free EUN to accommodate write to VUC %d\n", thisVUC);
				writeEUN = NFTL_findfreeblock(nftl, 1);
			}
			if (writeEUN == BLOCK_NIL) {
				printk(KERN_WARNING "Cannot make free space.\n");
				return BLOCK_NIL;
			}
			
			lastEUN = BLOCK_NIL;
			continue;
		}

		

		if (lastEUN != BLOCK_NIL) {
			thisVUC |= 0x8000; 
		} else {
			
			nftl->EUNtable[thisVUC] = writeEUN;
		}

		
		
		nftl->ReplUnitTable[writeEUN] = BLOCK_NIL;

		
		nftl_read_oob(mtd, writeEUN * nftl->EraseSize + 8, 8,
			      &retlen, (char *)&oob.u);

		oob.u.a.VirtUnitNum = oob.u.a.SpareVirtUnitNum = cpu_to_le16(thisVUC);

		nftl_write_oob(mtd, writeEUN * nftl->EraseSize + 8, 8,
			       &retlen, (char *)&oob.u);

		if (lastEUN != BLOCK_NIL) {
			
			nftl->ReplUnitTable[lastEUN] = writeEUN;
			
			nftl_read_oob(mtd, (lastEUN * nftl->EraseSize) + 8,
				      8, &retlen, (char *)&oob.u);

			oob.u.a.ReplUnitNum = oob.u.a.SpareReplUnitNum
				= cpu_to_le16(writeEUN);

			nftl_write_oob(mtd, (lastEUN * nftl->EraseSize) + 8,
				       8, &retlen, (char *)&oob.u);
		}

		return writeEUN;

	} while (silly2--);

	printk(KERN_WARNING "Error folding to make room for Virtual Unit Chain 0x%x\n",
	       thisVUC);
	return BLOCK_NIL;
}

static int nftl_writeblock(struct mtd_blktrans_dev *mbd, unsigned long block,
			   char *buffer)
{
	struct NFTLrecord *nftl = (void *)mbd;
	u16 writeEUN;
	unsigned long blockofs = (block * 512) & (nftl->EraseSize - 1);
	size_t retlen;
	struct nftl_oob oob;

	writeEUN = NFTL_findwriteunit(nftl, block);

	if (writeEUN == BLOCK_NIL) {
		printk(KERN_WARNING
		       "NFTL_writeblock(): Cannot find block to write to\n");
		
		return 1;
	}

	memset(&oob, 0xff, sizeof(struct nftl_oob));
	oob.b.Status = oob.b.Status1 = SECTOR_USED;

	nftl_write(nftl->mbd.mtd, (writeEUN * nftl->EraseSize) + blockofs,
		   512, &retlen, (char *)buffer, (char *)&oob);
	return 0;
}
#endif 

static int nftl_readblock(struct mtd_blktrans_dev *mbd, unsigned long block,
			  char *buffer)
{
	struct NFTLrecord *nftl = (void *)mbd;
	struct mtd_info *mtd = nftl->mbd.mtd;
	u16 lastgoodEUN;
	u16 thisEUN = nftl->EUNtable[block / (nftl->EraseSize / 512)];
	unsigned long blockofs = (block * 512) & (nftl->EraseSize - 1);
	unsigned int status;
	int silly = MAX_LOOPS;
	size_t retlen;
	struct nftl_bci bci;

	lastgoodEUN = BLOCK_NIL;

	if (thisEUN != BLOCK_NIL) {
		while (thisEUN < nftl->nb_blocks) {
			if (nftl_read_oob(mtd, (thisEUN * nftl->EraseSize) +
					  blockofs, 8, &retlen,
					  (char *)&bci) < 0)
				status = SECTOR_IGNORE;
			else
				status = bci.Status | bci.Status1;

			switch (status) {
			case SECTOR_FREE:
				
				goto the_end;
			case SECTOR_DELETED:
				lastgoodEUN = BLOCK_NIL;
				break;
			case SECTOR_USED:
				lastgoodEUN = thisEUN;
				break;
			case SECTOR_IGNORE:
				break;
			default:
				printk("Unknown status for block %ld in EUN %d: %x\n",
				       block, thisEUN, status);
				break;
			}

			if (!silly--) {
				printk(KERN_WARNING "Infinite loop in Virtual Unit Chain 0x%lx\n",
				       block / (nftl->EraseSize / 512));
				return 1;
			}
			thisEUN = nftl->ReplUnitTable[thisEUN];
		}
	}

 the_end:
	if (lastgoodEUN == BLOCK_NIL) {
		
		memset(buffer, 0, 512);
	} else {
		loff_t ptr = (lastgoodEUN * nftl->EraseSize) + blockofs;
		size_t retlen;
		int res = mtd_read(mtd, ptr, 512, &retlen, buffer);

		if (res < 0 && !mtd_is_bitflip(res))
			return -EIO;
	}
	return 0;
}

static int nftl_getgeo(struct mtd_blktrans_dev *dev,  struct hd_geometry *geo)
{
	struct NFTLrecord *nftl = (void *)dev;

	geo->heads = nftl->heads;
	geo->sectors = nftl->sectors;
	geo->cylinders = nftl->cylinders;

	return 0;
}



static struct mtd_blktrans_ops nftl_tr = {
	.name		= "nftl",
	.major		= NFTL_MAJOR,
	.part_bits	= NFTL_PARTN_BITS,
	.blksize 	= 512,
	.getgeo		= nftl_getgeo,
	.readsect	= nftl_readblock,
#ifdef CONFIG_NFTL_RW
	.writesect	= nftl_writeblock,
#endif
	.add_mtd	= nftl_add_mtd,
	.remove_dev	= nftl_remove_dev,
	.owner		= THIS_MODULE,
};

static int __init init_nftl(void)
{
	return register_mtd_blktrans(&nftl_tr);
}

static void __exit cleanup_nftl(void)
{
	deregister_mtd_blktrans(&nftl_tr);
}

module_init(init_nftl);
module_exit(cleanup_nftl);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("David Woodhouse <dwmw2@infradead.org>, Fabrice Bellard <fabrice.bellard@netgem.com> et al.");
MODULE_DESCRIPTION("Support code for NAND Flash Translation Layer, used on M-Systems DiskOnChip 2000 and Millennium");
MODULE_ALIAS_BLOCKDEV_MAJOR(NFTL_MAJOR);
