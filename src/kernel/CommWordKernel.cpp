/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Written (W) 1999-2007 Soeren Sonnenburg
 * Copyright (C) 1999-2007 Fraunhofer Institute FIRST and Max-Planck-Society
 */

#include "lib/common.h"
#include "kernel/CommWordKernel.h"
#include "features/Features.h"
#include "features/WordFeatures.h"
#include "lib/io.h"

CCommWordKernel::CCommWordKernel(LONG size, bool sign, ENormalizationType n) 
	: CSimpleKernel<WORD>(size), sqrtdiag_lhs(NULL), sqrtdiag_rhs(NULL), initialized(false), use_sign(sign), normalization(n)
{
	properties |= KP_LINADD;
	dictionary_size= 1<<(sizeof(WORD)*8);
	dictionary_weights = new DREAL[dictionary_size];
	SG_DEBUG( "using dictionary of %d bytes\n", dictionary_size);
	clear_normal();
}

CCommWordKernel::~CCommWordKernel() 
{
	cleanup();

	delete[] dictionary_weights;
}
  
void CCommWordKernel::remove_lhs() 
{ 
	delete_optimization();

#ifdef SVMLIGHT
	if (lhs)
		cache_reset();
#endif

	if (sqrtdiag_lhs != sqrtdiag_rhs)
		delete[] sqrtdiag_rhs;
	delete[] sqrtdiag_lhs;

	lhs = NULL ; 
	rhs = NULL ; 
	initialized = false;
	sqrtdiag_lhs = NULL;
	sqrtdiag_rhs = NULL;
}

void CCommWordKernel::remove_rhs()
{
#ifdef SVMLIGHT
	if (rhs)
		cache_reset();
#endif

	if (sqrtdiag_lhs != sqrtdiag_rhs)
		delete[] sqrtdiag_rhs;
	sqrtdiag_rhs = sqrtdiag_lhs;
	rhs = lhs;
}

bool CCommWordKernel::init(CFeatures* l, CFeatures* r)
{
	bool result=CSimpleKernel<WORD>::init(l,r);
	initialized = false;
	INT i;

	if (sqrtdiag_lhs != sqrtdiag_rhs)
	  delete[] sqrtdiag_rhs;
	sqrtdiag_rhs=NULL;
	delete[] sqrtdiag_lhs;
	sqrtdiag_lhs=NULL;

	sqrtdiag_lhs= new DREAL[lhs->get_num_vectors()];

	for (i=0; i<lhs->get_num_vectors(); i++)
		sqrtdiag_lhs[i]=1;

	if (l==r)
		sqrtdiag_rhs=sqrtdiag_lhs;
	else
	{
		sqrtdiag_rhs= new DREAL[rhs->get_num_vectors()];
		for (i=0; i<rhs->get_num_vectors(); i++)
			sqrtdiag_rhs[i]=1;
	}

	ASSERT(sqrtdiag_lhs);
	ASSERT(sqrtdiag_rhs);

	this->lhs=(CWordFeatures*) l;
	this->rhs=(CWordFeatures*) l;

	//compute normalize to 1 values
	for (i=0; i<lhs->get_num_vectors(); i++)
	{
		sqrtdiag_lhs[i]=sqrt(compute(i,i));

		//trap divide by zero exception
		if (sqrtdiag_lhs[i]==0)
			sqrtdiag_lhs[i]=1e-16;
	}

	// if lhs is different from rhs (train/test data)
	// compute also the normalization for rhs
	if (sqrtdiag_lhs!=sqrtdiag_rhs)
	{
		this->lhs=(CWordFeatures*) r;
		this->rhs=(CWordFeatures*) r;

		//compute normalize to 1 values
		for (i=0; i<rhs->get_num_vectors(); i++)
		{
			sqrtdiag_rhs[i]=sqrt(compute(i,i));

			//trap divide by zero exception
			if (sqrtdiag_rhs[i]==0)
				sqrtdiag_rhs[i]=1e-16;
		}
	}

	this->lhs=(CWordFeatures*) l;
	this->rhs=(CWordFeatures*) r;

	initialized = true;
	return result;
}

void CCommWordKernel::cleanup()
{
	delete_optimization();
	clear_normal();

	initialized=false;

	if (sqrtdiag_lhs != sqrtdiag_rhs)
		delete[] sqrtdiag_rhs;

	sqrtdiag_rhs=NULL;

	delete[] sqrtdiag_lhs;
	sqrtdiag_lhs=NULL;
}

bool CCommWordKernel::load_init(FILE* src)
{
	return false;
}

bool CCommWordKernel::save_init(FILE* dest)
{
	return false;
}
  
DREAL CCommWordKernel::compute(INT idx_a, INT idx_b)
{
	INT alen, blen;
	bool afree, bfree;

	WORD* avec=((CWordFeatures*) lhs)->get_feature_vector(idx_a, alen, afree);
	WORD* bvec=((CWordFeatures*) rhs)->get_feature_vector(idx_b, blen, bfree);

	// can only deal with strings of same length
	ASSERT(alen==blen);

	DREAL result=0;

	INT left_idx=0;
	INT right_idx=0;

	if (use_sign)
	{
		while (left_idx < alen && right_idx < blen)
		{
			if (avec[left_idx]==bvec[right_idx])
			{
				WORD sym=avec[left_idx];

				while (left_idx< alen && avec[left_idx]==sym)
					left_idx++;

				while (right_idx< blen && bvec[right_idx]==sym)
					right_idx++;

				result++;
			}
			else if (avec[left_idx]<bvec[right_idx])
				left_idx++;
			else
				right_idx++;
		}
	}
	else
	{
		while (left_idx < alen && right_idx < blen)
		{
			if (avec[left_idx]==bvec[right_idx])
			{
				INT old_left_idx=left_idx;
				INT old_right_idx=right_idx;

				WORD sym=avec[left_idx];

				while (left_idx< alen && avec[left_idx]==sym)
					left_idx++;

				while (right_idx< blen && bvec[right_idx]==sym)
					right_idx++;

				result+=((DREAL) (left_idx-old_left_idx)) * ((DREAL) (right_idx-old_right_idx));
			}
			else if (avec[left_idx]<bvec[right_idx])
				left_idx++;
			else
				right_idx++;
		}
	}

	((CWordFeatures*) lhs)->free_feature_vector(avec, idx_a, afree);
	((CWordFeatures*) rhs)->free_feature_vector(bvec, idx_b, bfree);

	if (initialized)
	{
		switch (normalization)
		{
			case NO_NORMALIZATION:
				return result;
			case SQRT_NORMALIZATION:
				return result/sqrt(sqrtdiag_lhs[idx_a]*sqrtdiag_rhs[idx_b]);
			case FULL_NORMALIZATION:
				return result/(sqrtdiag_lhs[idx_a]*sqrtdiag_rhs[idx_b]);
			case SQRTLEN_NORMALIZATION:
				return result/sqrt(sqrt(alen*blen));
			case LEN_NORMALIZATION:
				return result/sqrt(alen*blen);
			case SQLEN_NORMALIZATION:
				return result/(alen*blen);
			default:
            SG_ERROR( "Unknown Normalization in use!\n");
				return -CMath::INFTY;
		}
	}
	else
		return result;
}

void CCommWordKernel::add_to_normal(INT vec_idx, DREAL weight)
{
	INT len=-1;
	bool vfree;
	WORD* vec=((CWordFeatures*) lhs)->get_feature_vector(vec_idx, len, vfree);

	if (len>0)
	{
		int j, last_j=0;
		if (use_sign)
		{
			for (j=1; j<len; j++)
			{
				if (vec[j]==vec[j-1])
					continue;

				dictionary_weights[(int) vec[j-1]] += normalize_weight(weight, vec_idx, len, normalization);
			}

			dictionary_weights[(int) vec[len-1]] += normalize_weight(weight, vec_idx, len, normalization);
		}
		else
		{
			for (j=1; j<len; j++)
			{
				if (vec[j]==vec[j-1])
					continue;

				dictionary_weights[(int) vec[j-1]] += normalize_weight(weight*(j-last_j), vec_idx, len, normalization);
				last_j = j;
			}

			dictionary_weights[(int) vec[len-1]] += normalize_weight(weight*(len-last_j), vec_idx, len, normalization);
		}
		set_is_initialized(true);
	}
	((CWordFeatures*) lhs)->free_feature_vector(vec, vec_idx, vfree);
}

void CCommWordKernel::clear_normal()
{
	memset(dictionary_weights, 0, dictionary_size*sizeof(DREAL));
	set_is_initialized(false);
}

bool CCommWordKernel::init_optimization(INT count, INT *IDX, DREAL * weights) 
{
	delete_optimization();

	if (count<=0)
	{
		set_is_initialized(true);
		SG_DEBUG( "empty set of SVs\n");
		return true;
	}

	SG_DEBUG( "initializing CCommWordKernel optimization\n");

	for (int i=0; i<count; i++)
	{
		if ( (i % (count/10+1)) == 0)
			SG_PROGRESS(i, 0, count);

		add_to_normal(IDX[i], weights[i]);
	}

	SG_PRINT( "Done.         \n");
	
	set_is_initialized(true);
	return true;
}

bool CCommWordKernel::delete_optimization() 
{
	SG_DEBUG( "deleting CCommWordKernel optimization\n");

	clear_normal();
	return true;
}

DREAL CCommWordKernel::compute_optimized(INT i) 
{ 
	if (!get_is_initialized())
	{
      SG_ERROR( "CCommWordKernel optimization not initialized\n");
		return 0 ; 
	}

	DREAL result = 0;
	INT len = -1;
	bool vfree;
	WORD* vec=((CWordFeatures*) rhs)->get_feature_vector(i, len, vfree);

	int j, last_j=0;
	if (vec && len>0)
	{
		if (use_sign)
		{
			for (j=1; j<len; j++)
			{
				if (vec[j]==vec[j-1])
					continue;

				result += dictionary_weights[(int) vec[j-1]];
			}

			result += dictionary_weights[(int) vec[len-1]];
		}
		else
		{
			for (j=1; j<len; j++)
			{
				if (vec[j]==vec[j-1])
					continue;

				result += dictionary_weights[(int) vec[j-1]]*(j-last_j);
				last_j = j;
			}

			result += dictionary_weights[(int) vec[len-1]]*(len-last_j);
		}

		((CWordFeatures*) rhs)->free_feature_vector(vec, i, vfree);

		switch (normalization)
		{
			case NO_NORMALIZATION:
				return result;
			case SQRT_NORMALIZATION:
				return result/sqrt(sqrtdiag_rhs[i]);
			case FULL_NORMALIZATION:
				return result/sqrtdiag_rhs[i];
			case SQRTLEN_NORMALIZATION:
				return result/sqrt(sqrt(len));
			case LEN_NORMALIZATION:
				return result/sqrt(len);
			case SQLEN_NORMALIZATION:
				return result/len;
			default:
            SG_ERROR( "Unknown Normalization in use!\n");
				return -CMath::INFTY;
		}
	}
	return result;
}
