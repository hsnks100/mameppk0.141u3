
/* DES decryption, used by GD-ROM based titles (Naomi etc.) */

#include "emu.h"
#include "includes/naomi.h"

static UINT32 des_subkeys[32];

static const UINT32 DES_LEFTSWAP[] =
{
  0x00000000, 0x00000001, 0x00000100, 0x00000101, 0x00010000, 0x00010001, 0x00010100, 0x00010101,
  0x01000000, 0x01000001, 0x01000100, 0x01000101, 0x01010000, 0x01010001, 0x01010100, 0x01010101
};

static const UINT32 DES_RIGHTSWAP[] =
{
  0x00000000, 0x01000000, 0x00010000, 0x01010000, 0x00000100, 0x01000100, 0x00010100, 0x01010100,
  0x00000001, 0x01000001, 0x00010001, 0x01010001, 0x00000101, 0x01000101, 0x00010101, 0x01010101,
};

static const UINT32 DES_SBOX1[] =
{
  0x00808200, 0x00000000, 0x00008000, 0x00808202, 0x00808002, 0x00008202, 0x00000002, 0x00008000,
  0x00000200, 0x00808200, 0x00808202, 0x00000200, 0x00800202, 0x00808002, 0x00800000, 0x00000002,
  0x00000202, 0x00800200, 0x00800200, 0x00008200, 0x00008200, 0x00808000, 0x00808000, 0x00800202,
  0x00008002, 0x00800002, 0x00800002, 0x00008002, 0x00000000, 0x00000202, 0x00008202, 0x00800000,
  0x00008000, 0x00808202, 0x00000002, 0x00808000, 0x00808200, 0x00800000, 0x00800000, 0x00000200,
  0x00808002, 0x00008000, 0x00008200, 0x00800002, 0x00000200, 0x00000002, 0x00800202, 0x00008202,
  0x00808202, 0x00008002, 0x00808000, 0x00800202, 0x00800002, 0x00000202, 0x00008202, 0x00808200,
  0x00000202, 0x00800200, 0x00800200, 0x00000000, 0x00008002, 0x00008200, 0x00000000, 0x00808002
};

static const UINT32 DES_SBOX2[] =
{
  0x40084010, 0x40004000, 0x00004000, 0x00084010, 0x00080000, 0x00000010, 0x40080010, 0x40004010,
  0x40000010, 0x40084010, 0x40084000, 0x40000000, 0x40004000, 0x00080000, 0x00000010, 0x40080010,
  0x00084000, 0x00080010, 0x40004010, 0x00000000, 0x40000000, 0x00004000, 0x00084010, 0x40080000,
  0x00080010, 0x40000010, 0x00000000, 0x00084000, 0x00004010, 0x40084000, 0x40080000, 0x00004010,
  0x00000000, 0x00084010, 0x40080010, 0x00080000, 0x40004010, 0x40080000, 0x40084000, 0x00004000,
  0x40080000, 0x40004000, 0x00000010, 0x40084010, 0x00084010, 0x00000010, 0x00004000, 0x40000000,
  0x00004010, 0x40084000, 0x00080000, 0x40000010, 0x00080010, 0x40004010, 0x40000010, 0x00080010,
  0x00084000, 0x00000000, 0x40004000, 0x00004010, 0x40000000, 0x40080010, 0x40084010, 0x00084000
};

static const UINT32 DES_SBOX3[] =
{
  0x00000104, 0x04010100, 0x00000000, 0x04010004, 0x04000100, 0x00000000, 0x00010104, 0x04000100,
  0x00010004, 0x04000004, 0x04000004, 0x00010000, 0x04010104, 0x00010004, 0x04010000, 0x00000104,
  0x04000000, 0x00000004, 0x04010100, 0x00000100, 0x00010100, 0x04010000, 0x04010004, 0x00010104,
  0x04000104, 0x00010100, 0x00010000, 0x04000104, 0x00000004, 0x04010104, 0x00000100, 0x04000000,
  0x04010100, 0x04000000, 0x00010004, 0x00000104, 0x00010000, 0x04010100, 0x04000100, 0x00000000,
  0x00000100, 0x00010004, 0x04010104, 0x04000100, 0x04000004, 0x00000100, 0x00000000, 0x04010004,
  0x04000104, 0x00010000, 0x04000000, 0x04010104, 0x00000004, 0x00010104, 0x00010100, 0x04000004,
  0x04010000, 0x04000104, 0x00000104, 0x04010000, 0x00010104, 0x00000004, 0x04010004, 0x00010100
};

static const UINT32 DES_SBOX4[] =
{
  0x80401000, 0x80001040, 0x80001040, 0x00000040, 0x00401040, 0x80400040, 0x80400000, 0x80001000,
  0x00000000, 0x00401000, 0x00401000, 0x80401040, 0x80000040, 0x00000000, 0x00400040, 0x80400000,
  0x80000000, 0x00001000, 0x00400000, 0x80401000, 0x00000040, 0x00400000, 0x80001000, 0x00001040,
  0x80400040, 0x80000000, 0x00001040, 0x00400040, 0x00001000, 0x00401040, 0x80401040, 0x80000040,
  0x00400040, 0x80400000, 0x00401000, 0x80401040, 0x80000040, 0x00000000, 0x00000000, 0x00401000,
  0x00001040, 0x00400040, 0x80400040, 0x80000000, 0x80401000, 0x80001040, 0x80001040, 0x00000040,
  0x80401040, 0x80000040, 0x80000000, 0x00001000, 0x80400000, 0x80001000, 0x00401040, 0x80400040,
  0x80001000, 0x00001040, 0x00400000, 0x80401000, 0x00000040, 0x00400000, 0x00001000, 0x00401040
};

static const UINT32 DES_SBOX5[] =
{
  0x00000080, 0x01040080, 0x01040000, 0x21000080, 0x00040000, 0x00000080, 0x20000000, 0x01040000,
  0x20040080, 0x00040000, 0x01000080, 0x20040080, 0x21000080, 0x21040000, 0x00040080, 0x20000000,
  0x01000000, 0x20040000, 0x20040000, 0x00000000, 0x20000080, 0x21040080, 0x21040080, 0x01000080,
  0x21040000, 0x20000080, 0x00000000, 0x21000000, 0x01040080, 0x01000000, 0x21000000, 0x00040080,
  0x00040000, 0x21000080, 0x00000080, 0x01000000, 0x20000000, 0x01040000, 0x21000080, 0x20040080,
  0x01000080, 0x20000000, 0x21040000, 0x01040080, 0x20040080, 0x00000080, 0x01000000, 0x21040000,
  0x21040080, 0x00040080, 0x21000000, 0x21040080, 0x01040000, 0x00000000, 0x20040000, 0x21000000,
  0x00040080, 0x01000080, 0x20000080, 0x00040000, 0x00000000, 0x20040000, 0x01040080, 0x20000080
};

static const UINT32 DES_SBOX6[] =
{
  0x10000008, 0x10200000, 0x00002000, 0x10202008, 0x10200000, 0x00000008, 0x10202008, 0x00200000,
  0x10002000, 0x00202008, 0x00200000, 0x10000008, 0x00200008, 0x10002000, 0x10000000, 0x00002008,
  0x00000000, 0x00200008, 0x10002008, 0x00002000, 0x00202000, 0x10002008, 0x00000008, 0x10200008,
  0x10200008, 0x00000000, 0x00202008, 0x10202000, 0x00002008, 0x00202000, 0x10202000, 0x10000000,
  0x10002000, 0x00000008, 0x10200008, 0x00202000, 0x10202008, 0x00200000, 0x00002008, 0x10000008,
  0x00200000, 0x10002000, 0x10000000, 0x00002008, 0x10000008, 0x10202008, 0x00202000, 0x10200000,
  0x00202008, 0x10202000, 0x00000000, 0x10200008, 0x00000008, 0x00002000, 0x10200000, 0x00202008,
  0x00002000, 0x00200008, 0x10002008, 0x00000000, 0x10202000, 0x10000000, 0x00200008, 0x10002008
};

static const UINT32 DES_SBOX7[] =
{
  0x00100000, 0x02100001, 0x02000401, 0x00000000, 0x00000400, 0x02000401, 0x00100401, 0x02100400,
  0x02100401, 0x00100000, 0x00000000, 0x02000001, 0x00000001, 0x02000000, 0x02100001, 0x00000401,
  0x02000400, 0x00100401, 0x00100001, 0x02000400, 0x02000001, 0x02100000, 0x02100400, 0x00100001,
  0x02100000, 0x00000400, 0x00000401, 0x02100401, 0x00100400, 0x00000001, 0x02000000, 0x00100400,
  0x02000000, 0x00100400, 0x00100000, 0x02000401, 0x02000401, 0x02100001, 0x02100001, 0x00000001,
  0x00100001, 0x02000000, 0x02000400, 0x00100000, 0x02100400, 0x00000401, 0x00100401, 0x02100400,
  0x00000401, 0x02000001, 0x02100401, 0x02100000, 0x00100400, 0x00000000, 0x00000001, 0x02100401,
  0x00000000, 0x00100401, 0x02100000, 0x00000400, 0x02000001, 0x02000400, 0x00000400, 0x00100001
};

static const UINT32 DES_SBOX8[] =
{
  0x08000820, 0x00000800, 0x00020000, 0x08020820, 0x08000000, 0x08000820, 0x00000020, 0x08000000,
  0x00020020, 0x08020000, 0x08020820, 0x00020800, 0x08020800, 0x00020820, 0x00000800, 0x00000020,
  0x08020000, 0x08000020, 0x08000800, 0x00000820, 0x00020800, 0x00020020, 0x08020020, 0x08020800,
  0x00000820, 0x00000000, 0x00000000, 0x08020020, 0x08000020, 0x08000800, 0x00020820, 0x00020000,
  0x00020820, 0x00020000, 0x08020800, 0x00000800, 0x00000020, 0x08020020, 0x00000800, 0x00020820,
  0x08000800, 0x00000020, 0x08000020, 0x08020000, 0x08020020, 0x08000000, 0x00020000, 0x08000820,
  0x00000000, 0x08020820, 0x00020020, 0x08000020, 0x08020000, 0x08000800, 0x08000820, 0x00000000,
  0x08020820, 0x00020800, 0x00020800, 0x00000820, 0x00000820, 0x00020020, 0x08000000, 0x08020800
};

static const UINT32 DES_MASK_TABLE[] =
{
	0x24000000, 0x10000000, 0x08000000, 0x02080000, 0x01000000,
	0x00200000, 0x00100000, 0x00040000, 0x00020000, 0x00010000,
	0x00002000, 0x00001000, 0x00000800, 0x00000400, 0x00000200,
	0x00000100, 0x00000020, 0x00000010, 0x00000008, 0x00000004,
	0x00000002, 0x00000001, 0x20000000, 0x10000000, 0x08000000,
	0x04000000, 0x02000000, 0x01000000, 0x00200000, 0x00100000,
	0x00080000, 0x00040000, 0x00020000, 0x00010000, 0x00002000,
	0x00001000, 0x00000808, 0x00000400, 0x00000200, 0x00000100,
	0x00000020, 0x00000011, 0x00000004, 0x00000002
};

static const UINT8 DES_ROTATE_TABLE[16] =
{
  1, 1, 2, 2, 2, 2, 2, 2, 1, 2, 2, 2, 2, 2, 2, 1
};


INLINE void permutate(UINT32*a, UINT32*b, UINT32 m, int shift)
{
	UINT32 temp;
    temp = ((*a>>shift) ^ *b) & m;
    *a ^= temp<<shift;
    *b ^= temp;
}


static void des_generate_subkeys(const UINT64 key, UINT32 *subkeys)
{
	UINT32 l, r;
	int round;

	l  = (key & 0xffffffff00000000ULL)>>32;
	r = (key & 0x00000000ffffffffULL)>>0;;

	permutate(&r, &l, 0x0f0f0f0f, 4);
	permutate(&r, &l, 0x10101010, 0);

	l  = (DES_LEFTSWAP[(l >> 0)  & 0xf] << 3) |
	        (DES_LEFTSWAP[(l >> 8)  & 0xf] << 2) |
            (DES_LEFTSWAP[(l >> 16) & 0xf] << 1) |
	        (DES_LEFTSWAP[(l >> 24) & 0xf] << 0) |
	        (DES_LEFTSWAP[(l >> 5)  & 0xf] << 7) |
	        (DES_LEFTSWAP[(l >> 13) & 0xf] << 6) |
	        (DES_LEFTSWAP[(l >> 21) & 0xf] << 5) |
	        (DES_LEFTSWAP[(l >> 29) & 0xf] << 4);

	r = (DES_RIGHTSWAP[(r >> 1)  & 0xf] << 3) |
	        (DES_RIGHTSWAP[(r >> 9)  & 0xf] << 2) |
		    (DES_RIGHTSWAP[(r >> 17) & 0xf] << 1) |
		    (DES_RIGHTSWAP[(r >> 25) & 0xf] << 0) |
		    (DES_RIGHTSWAP[(r >> 4)  & 0xf] << 7) |
		    (DES_RIGHTSWAP[(r >> 12) & 0xf] << 6) |
		    (DES_RIGHTSWAP[(r >> 20) & 0xf] << 5) |
		    (DES_RIGHTSWAP[(r >> 28) & 0xf] << 4);

	l &= 0x0fffffff;
	r &= 0x0fffffff;


	for (round = 0; round < 16; round++)
	{
		l = ((l << DES_ROTATE_TABLE[round]) | (l >> (28 - DES_ROTATE_TABLE[round]))) & 0x0fffffff;
		r = ((r << DES_ROTATE_TABLE[round]) | (r >> (28 - DES_ROTATE_TABLE[round]))) & 0x0fffffff;

		subkeys[round*2] =
			((l << 4)  & DES_MASK_TABLE[0]) |
		    ((l << 28) & DES_MASK_TABLE[1]) |
		    ((l << 14) & DES_MASK_TABLE[2]) |
		    ((l << 18) & DES_MASK_TABLE[3]) |
		    ((l << 6)  & DES_MASK_TABLE[4]) |
		    ((l << 9)  & DES_MASK_TABLE[5]) |
		    ((l >> 1)  & DES_MASK_TABLE[6]) |
		    ((l << 10) & DES_MASK_TABLE[7]) |
		    ((l << 2)  & DES_MASK_TABLE[8]) |
		    ((l >> 10) & DES_MASK_TABLE[9]) |
		    ((r >> 13) & DES_MASK_TABLE[10])|
		    ((r >> 4)  & DES_MASK_TABLE[11])|
		    ((r << 6)  & DES_MASK_TABLE[12])|
		    ((r >> 1)  & DES_MASK_TABLE[13])|
		    ((r >> 14) & DES_MASK_TABLE[14])|
		    ((r >> 0)  & DES_MASK_TABLE[15])|
		    ((r >> 5)  & DES_MASK_TABLE[16])|
		    ((r >> 10) & DES_MASK_TABLE[17])|
		    ((r >> 3)  & DES_MASK_TABLE[18])|
		    ((r >> 18) & DES_MASK_TABLE[19])|
		    ((r >> 26) & DES_MASK_TABLE[20])|
		    ((r >> 24) & DES_MASK_TABLE[21]);

		subkeys[round*2+1] =
		    ((l << 15) & DES_MASK_TABLE[22])|
		    ((l << 17) & DES_MASK_TABLE[23])|
		    ((l << 10) & DES_MASK_TABLE[24])|
		    ((l << 22) & DES_MASK_TABLE[25])|
		    ((l >> 2)  & DES_MASK_TABLE[26])|
		    ((l << 1)  & DES_MASK_TABLE[27])|
		    ((l << 16) & DES_MASK_TABLE[28])|
		    ((l << 11) & DES_MASK_TABLE[29])|
		    ((l << 3)  & DES_MASK_TABLE[30])|
		    ((l >> 6)  & DES_MASK_TABLE[31])|
		    ((l << 15) & DES_MASK_TABLE[32])|
		    ((l >> 4)  & DES_MASK_TABLE[33])|
		    ((r >> 2)  & DES_MASK_TABLE[34])|
		    ((r << 8)  & DES_MASK_TABLE[35])|
		    ((r >> 14) & DES_MASK_TABLE[36])|
		    ((r >> 9)  & DES_MASK_TABLE[37])|
		    ((r >> 0)  & DES_MASK_TABLE[38])|
		    ((r << 7)  & DES_MASK_TABLE[39])|
		    ((r >> 7)  & DES_MASK_TABLE[40])|
		    ((r >> 3)  & DES_MASK_TABLE[41])|
		    ((r << 2)  & DES_MASK_TABLE[42])|
		    ((r >> 21) & DES_MASK_TABLE[43]);
    }
}


static UINT64 des_encrypt_decrypt(int decrypt, UINT64 ret)
{
	UINT32 l;
	UINT32 r;
	int i;
	int subkey;

	r = (ret & 0x00000000ffffffffULL) >> 0;
	l = (ret & 0xffffffff00000000ULL) >> 32;

	permutate(&l,  &r, 0x0f0f0f0f, 4);
	permutate(&l,  &r, 0x0000ffff, 16);
	permutate(&r,  &l, 0x33333333, 2);
	permutate(&r,  &l, 0x00ff00ff, 8);
	permutate(&l,  &r, 0x55555555, 1);

	if (decrypt) subkey = 30;
	else subkey = 0;

	for (i = 0; i < 32 ; i+=4)
	{
		UINT32 temp;

    	temp = ((r<<1) | (r>>31)) ^ des_subkeys[subkey];
    	l ^= DES_SBOX8[ (temp>>0)  & 0x3f ];
    	l ^= DES_SBOX6[ (temp>>8)  & 0x3f ];
    	l ^= DES_SBOX4[ (temp>>16) & 0x3f ];
    	l ^= DES_SBOX2[ (temp>>24) & 0x3f ];
		subkey++;

		temp = ((r>>3) | (r<<29)) ^ des_subkeys[subkey];
    	l ^= DES_SBOX7[ (temp>>0)  & 0x3f ];
    	l ^= DES_SBOX5[ (temp>>8)  & 0x3f ];
    	l ^= DES_SBOX3[ (temp>>16) & 0x3f ];
    	l ^= DES_SBOX1[ (temp>>24) & 0x3f ];
		subkey++;
		if (decrypt) subkey-=4;

    	temp = ((l<<1) | (l>>31)) ^ des_subkeys[subkey];
    	r ^= DES_SBOX8[ (temp>>0)  & 0x3f ];
    	r ^= DES_SBOX6[ (temp>>8)  & 0x3f ];
    	r ^= DES_SBOX4[ (temp>>16) & 0x3f ];
    	r ^= DES_SBOX2[ (temp>>24) & 0x3f ];
		subkey++;

		temp = ((l>>3) | (l<<29)) ^ des_subkeys[subkey];
    	r ^= DES_SBOX7[ (temp>>0)  & 0x3f ];
    	r ^= DES_SBOX5[ (temp>>8)  & 0x3f ];
    	r ^= DES_SBOX3[ (temp>>16) & 0x3f ];
    	r ^= DES_SBOX1[ (temp>>24) & 0x3f ];
		subkey++;
		if (decrypt) subkey-=4;
	}

	permutate(&r, &l,  0x55555555, 1);
	permutate(&l, &r,  0x00ff00ff, 8);
	permutate(&l, &r,  0x33333333, 2);
	permutate(&r, &l,  0x0000ffff, 16);
	permutate(&r, &l,  0x0f0f0f0f, 4);

	return ((UINT64)r << 32) | ((UINT64)l);
}

static UINT64 rev64(UINT64 src)
{
	UINT64 ret;

	ret = ((src & 0x00000000000000ffULL) << 56)
	    | ((src & 0x000000000000ff00ULL) << 40)
	    | ((src & 0x0000000000ff0000ULL) << 24)
	    | ((src & 0x00000000ff000000ULL) << 8 )
	    | ((src & 0x000000ff00000000ULL) >> 8 )
	    | ((src & 0x0000ff0000000000ULL) >> 24)
	    | ((src & 0x00ff000000000000ULL) >> 40)
	    | ((src & 0xff00000000000000ULL) >> 56);

	return ret;
}

static UINT64 read_to_qword(UINT8* region)
{
	int i;
	UINT64 ret = 0;

	for (i=0;i<8;i++)
	{
		UINT8 byte = region[i];
		ret += (UINT64)byte << (56-(8*i));
	}

	return ret;
}


static void write_from_qword(UINT8* region, UINT64 qword)
{
	int i;
	for (i=0;i<8;i++)
	{
		region[i] = qword >> (56-(i*8));
	}
}

void naomi_game_decrypt(running_machine* machine, UINT64 key, UINT8* region, int length)
{
	int i;

	des_generate_subkeys (rev64(key), des_subkeys);

//	#ifdef MAME_DEBUG
	/* save the original file */
	{
		FILE *fp;
		char filename[256];
		sprintf(filename,"encrypted %s", machine->gamedrv->name);
		fp=fopen(filename, "w+b");
		if (fp)
		{
			fwrite(region, length, 1, fp);
			fclose(fp);
		}
	}
//	#endif

	for(i=0;i<length;i+=8)
	{
		UINT64 ret;
		ret = read_to_qword(region+i);
		ret = rev64(ret);
		ret = des_encrypt_decrypt(1, ret);
		ret = rev64(ret);
		write_from_qword(region+i, ret);
	}

//	#ifdef MAME_DEBUG
	/* save the decrypted file */
	{
		FILE *fp;
		char filename[256];
		sprintf(filename,"decrypted %s", machine->gamedrv->name);
		fp=fopen(filename, "w+b");
		if (fp)
		{
			fwrite(region, length, 1, fp);
			fclose(fp);
		}
	}
//	#endif
}

