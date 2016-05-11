void eq_MMX(unsigned char *dest, int dstride, unsigned char *src, int sstride,
		    int w, int h, int brightness, int contrast)
{
	int i;
	int pel;
	int dstep = dstride-w;
	int sstep = sstride-w;
	short brvec[4];
	short contvec[4];

	contrast = ((contrast+100)*256*16)/100;
	brightness = ((brightness+100)*511)/200-128 - contrast/32;

	brvec[0] = brvec[1] = brvec[2] = brvec[3] = brightness;
	contvec[0] = contvec[1] = contvec[2] = contvec[3] = contrast;
		
	while (h--) {
		asm volatile (
			"movq (%5), %%mm3 \n\t"
			"movq (%6), %%mm4 \n\t"
			"pxor %%mm0, %%mm0 \n\t"
			"movl %4, %%eax\n\t"
                       ".balign 16 \n\t"
			"1: \n\t"
			"movq (%0), %%mm1 \n\t"
			"movq (%0), %%mm2 \n\t"
			"punpcklbw %%mm0, %%mm1 \n\t"
			"punpckhbw %%mm0, %%mm2 \n\t"
			"psllw $4, %%mm1 \n\t"
			"psllw $4, %%mm2 \n\t"
			"pmulhw %%mm4, %%mm1 \n\t"
			"pmulhw %%mm4, %%mm2 \n\t"
			"paddw %%mm3, %%mm1 \n\t"
			"paddw %%mm3, %%mm2 \n\t"
			"packuswb %%mm2, %%mm1 \n\t"
			"addl $8, %0 \n\t"
			"movq %%mm1, (%1) \n\t"
			"addl $8, %1 \n\t"
			"decl %%eax \n\t"
			"jnz 1b \n\t"
			: "=r" (src), "=r" (dest)
			: "0" (src), "1" (dest), "r" (w>>3), "r" (brvec), "r" (contvec)
			: "%eax"
		);

		for (i = w&7; i; i--)
		{
			pel = ((*src++* contrast)>>12) + brightness;
			if(pel&768) pel = (-pel)>>31;
			*dest++ = pel;
		}

		src += sstep;
		dest += dstep;
	}
	asm volatile ( "emms \n\t" ::: "memory" );
}
