

/*
 * -- SuperLU routine (version 2.0) --
 * Univ. of California Berkeley, Xerox Palo Alto Research Center,
 * and Lawrence Berkeley National Lab.
 * November 15, 1997
 *
 */
/*
 * File name:	zgsrfs.c
 * History:     Modified from lapack routine ZGERFS
 */
#include <math.h>
#include "zsp_defs.h"
#include "util.h"

void
zgsrfs(char *trans, SuperMatrix *A, SuperMatrix *L, SuperMatrix *U,
       int *perm_r, int *perm_c, char *equed, double *R, double *C,
       SuperMatrix *B, SuperMatrix *X, 
       double *ferr, double *berr, int *info)
{
/*
 *   Purpose   
 *   =======   
 *
 *   ZGSRFS improves the computed solution to a system of linear   
 *   equations and provides error bounds and backward error estimates for 
 *   the solution.   
 *
 *   If equilibration was performed, the system becomes:
 *           (diag(R)*A_original*diag(C)) * X = diag(R)*B_original.
 *
 *   See supermatrix.h for the definition of 'SuperMatrix' structure.
 *
 *   Arguments   
 *   =========   
 *
 *   trans   (input) char*
 *           Specifies the form of the system of equations:   
 *           = 'N':  A * X = B     (No transpose)   
 *           = 'T':  A**T * X = B  (Transpose)   
 *           = 'C':  A**H * X = B  (Conjugate transpose = Transpose)
 *   
 *   A       (input) SuperMatrix*
 *           The original matrix A in the system, or the scaled A if
 *           equilibration was done. The type of A can be:
 *           Stype = NC, Dtype = _Z, Mtype = GE.
 *    
 *   L       (input) SuperMatrix*
 *	     The factor L from the factorization Pr*A*Pc=L*U. Use
 *           compressed row subscripts storage for supernodes, 
 *           i.e., L has types: Stype = SC, Dtype = _Z, Mtype = TRLU.
 * 
 *   U       (input) SuperMatrix*
 *           The factor U from the factorization Pr*A*Pc=L*U as computed by
 *           zgstrf(). Use column-wise storage scheme, 
 *           i.e., U has types: Stype = NC, Dtype = _Z, Mtype = TRU.
 *
 *   perm_r  (input) int*, dimension (A->nrow)
 *           Row permutation vector, which defines the permutation matrix Pr;
 *           perm_r[i] = j means row i of A is in position j in Pr*A.
 *
 *   perm_c  (input) int*, dimension (A->ncol)
 *	     Column permutation vector, which defines the 
 *           permutation matrix Pc; perm_c[i] = j means column i of A is 
 *           in position j in A*Pc.
 *
 *   equed   (input) Specifies the form of equilibration that was done.
 *           = 'N': No equilibration.
 *           = 'R': Row equilibration, i.e., A was premultiplied by diag(R).
 *           = 'C': Column equilibration, i.e., A was postmultiplied by
 *                  diag(C).
 *           = 'B': Both row and column equilibration, i.e., A was replaced 
 *                  by diag(R)*A*diag(C).
 *
 *   R       (input) double*, dimension (A->nrow)
 *           The row scale factors for A.
 *           If equed = 'R' or 'B', A is premultiplied by diag(R).
 *           If equed = 'N' or 'C', R is not accessed.
 * 
 *   C       (input) double*, dimension (A->ncol)
 *           The column scale factors for A.
 *           If equed = 'C' or 'B', A is postmultiplied by diag(C).
 *           If equed = 'N' or 'R', C is not accessed.
 *
 *   B       (input) SuperMatrix*
 *           B has types: Stype = DN, Dtype = _Z, Mtype = GE.
 *           The right hand side matrix B.
 *           if equed = 'R' or 'B', B is premultiplied by diag(R).
 *
 *   X       (input/output) SuperMatrix*
 *           X has types: Stype = DN, Dtype = _Z, Mtype = GE.
 *           On entry, the solution matrix X, as computed by zgstrs().
 *           On exit, the improved solution matrix X.
 *           if *equed = 'C' or 'B', X should be premultiplied by diag(C)
 *               in order to obtain the solution to the original system.
 *
 *   FERR    (output) double*, dimension (B->ncol)   
 *           The estimated forward error bound for each solution vector   
 *           X(j) (the j-th column of the solution matrix X).   
 *           If XTRUE is the true solution corresponding to X(j), FERR(j) 
 *           is an estimated upper bound for the magnitude of the largest 
 *           element in (X(j) - XTRUE) divided by the magnitude of the   
 *           largest element in X(j).  The estimate is as reliable as   
 *           the estimate for RCOND, and is almost always a slight   
 *           overestimate of the true error.
 *
 *   BERR    (output) double*, dimension (B->ncol)   
 *           The componentwise relative backward error of each solution   
 *           vector X(j) (i.e., the smallest relative change in   
 *           any element of A or B that makes X(j) an exact solution).
 *
 *   info    (output) int*   
 *           = 0:  successful exit   
 *            < 0:  if INFO = -i, the i-th argument had an illegal value   
 *
 *    Internal Parameters   
 *    ===================   
 *
 *    ITMAX is the maximum number of steps of iterative refinement.   
 *
 */  

#define ITMAX 5
    
    /* Table of constant values */
    int    ione = 1;
    doublecomplex ndone = {-1., 0.};
    doublecomplex done = {1., 0.};
    
    /* Local variables */
    NCformat *Astore;
    doublecomplex   *Aval;
    SuperMatrix Bjcol;
    DNformat *Bstore, *Xstore, *Bjcol_store;
    doublecomplex   *Bmat, *Xmat, *Bptr, *Xptr;
    int      kase;
    double   safe1, safe2;
    int      i, j, k, irow, nz, count, notran, rowequ, colequ;
    int      ldb, ldx, nrhs;
    double   s, xk, lstres, eps, safmin;
    char     transt[1];
    doublecomplex   *work;
    double   *rwork;
    int      *iwork;
    extern double dlamch_(char *);
    extern int zlacon_(int *, doublecomplex *, doublecomplex *, double *, int *);
#ifdef _CRAY
    extern int CCOPY(int *, doublecomplex *, int *, doublecomplex *, int *);
    extern int CSAXPY(int *, doublecomplex *, doublecomplex *, int *, doublecomplex *, int *);
#else
    extern int zcopy_(int *, doublecomplex *, int *, doublecomplex *, int *);
    extern int zaxpy_(int *, doublecomplex *, doublecomplex *, int *, doublecomplex *, int *);
#endif

    Astore = A->Store;
    Aval   = Astore->nzval;
    Bstore = B->Store;
    Xstore = X->Store;
    Bmat   = Bstore->nzval;
    Xmat   = Xstore->nzval;
    ldb    = Bstore->lda;
    ldx    = Xstore->lda;
    nrhs   = B->ncol;
    
    /* Test the input parameters */
    *info = 0;
    notran = lsame_(trans, "N");
    if ( !notran && !lsame_(trans, "T") && !lsame_(trans, "C"))	*info = -1;
    else if ( A->nrow != A->ncol || A->nrow < 0 ||
	      A->Stype != NC || A->Dtype != _Z || A->Mtype != GE )
	*info = -2;
    else if ( L->nrow != L->ncol || L->nrow < 0 ||
 	      L->Stype != SC || L->Dtype != _Z || L->Mtype != TRLU )
	*info = -3;
    else if ( U->nrow != U->ncol || U->nrow < 0 ||
 	      U->Stype != NC || U->Dtype != _Z || U->Mtype != TRU )
	*info = -4;
    else if ( ldb < MAX(0, A->nrow) ||
 	      B->Stype != DN || B->Dtype != _Z || B->Mtype != GE )
        *info = -10;
    else if ( ldx < MAX(0, A->nrow) ||
 	      X->Stype != DN || X->Dtype != _Z || X->Mtype != GE )
	*info = -11;
    if (*info != 0) {
	i = -(*info);
	xerbla_("zgsrfs", &i);
	return;
    }

    /* Quick return if possible */
    if ( A->nrow == 0 || nrhs == 0) {
	for (j = 0; j < nrhs; ++j) {
	    ferr[j] = 0.;
	    berr[j] = 0.;
	}
	return;
    }

    rowequ = lsame_(equed, "R") || lsame_(equed, "B");
    colequ = lsame_(equed, "C") || lsame_(equed, "B");
    
    /* Allocate working space */
    work = doublecomplexMalloc(2*A->nrow);
    rwork = (double *) SUPERLU_MALLOC( A->nrow * sizeof(double) );
    iwork = intMalloc(A->nrow);
    if ( !work || !rwork || !iwork ) 
        ABORT("Malloc fails for work/rwork/iwork.");
    
    if ( notran ) {
	*(unsigned char *)transt = 'T';
    } else {
	*(unsigned char *)transt = 'N';
    }

    /* NZ = maximum number of nonzero elements in each row of A, plus 1 */
    nz     = A->ncol + 1;
    eps    = dlamch_("Epsilon");
    safmin = dlamch_("Safe minimum");
    safe1  = nz * safmin;
    safe2  = safe1 / eps;

    /* Compute the number of nonzeros in each row (or column) of A */
    for (i = 0; i < A->nrow; ++i) iwork[i] = 0;
    if ( notran ) {
	for (k = 0; k < A->ncol; ++k)
	    for (i = Astore->colptr[k]; i < Astore->colptr[k+1]; ++i) 
		++iwork[Astore->rowind[i]];
    } else {
	for (k = 0; k < A->ncol; ++k)
	    iwork[k] = Astore->colptr[k+1] - Astore->colptr[k];
    }	

    /* Copy one column of RHS B into Bjcol. */
    Bjcol.Stype = B->Stype;
    Bjcol.Dtype = B->Dtype;
    Bjcol.Mtype = B->Mtype;
    Bjcol.nrow  = B->nrow;
    Bjcol.ncol  = 1;
    Bjcol.Store = (void *) SUPERLU_MALLOC( sizeof(DNformat) );
    if ( !Bjcol.Store ) ABORT("SUPERLU_MALLOC fails for Bjcol.Store");
    Bjcol_store = Bjcol.Store;
    Bjcol_store->lda = ldb;
    Bjcol_store->nzval = work; /* address aliasing */
	
    /* Do for each right hand side ... */
    for (j = 0; j < nrhs; ++j) {
	count = 0;
	lstres = 3.;
	Bptr = &Bmat[j*ldb];
	Xptr = &Xmat[j*ldx];

	while (1) { /* Loop until stopping criterion is satisfied. */

	    /* Compute residual R = B - op(A) * X,   
	       where op(A) = A, A**T, or A**H, depending on TRANS. */
	    
#ifdef _CRAY
	    CCOPY(&A->nrow, Bptr, &ione, work, &ione);
#else
	    zcopy_(&A->nrow, Bptr, &ione, work, &ione);
#endif
	    sp_zgemv(trans, ndone, A, Xptr, ione, done, work, ione);

	    /* Compute componentwise relative backward error from formula 
	       max(i) ( abs(R(i)) / ( abs(op(A))*abs(X) + abs(B) )(i) )   
	       where abs(Z) is the componentwise absolute value of the matrix
	       or vector Z.  If the i-th component of the denominator is less
	       than SAFE2, then SAFE1 is added to the i-th component of the   
	       numerator and denominator before dividing. */

	    for (i = 0; i < A->nrow; ++i) rwork[i] = z_abs1( &Bptr[i] );
	    
	    /* Compute abs(op(A))*abs(X) + abs(B). */
	    if (notran) {
		for (k = 0; k < A->ncol; ++k) {
		    xk = z_abs1( &Xptr[k] );
		    for (i = Astore->colptr[k]; i < Astore->colptr[k+1]; ++i)
			rwork[Astore->rowind[i]] += z_abs1(&Aval[i]) * xk;
		}
	    } else {
		for (k = 0; k < A->ncol; ++k) {
		    s = 0.;
		    for (i = Astore->colptr[k]; i < Astore->colptr[k+1]; ++i) {
			irow = Astore->rowind[i];
			s += z_abs1(&Aval[i]) * z_abs1(&Xptr[irow]);
		    }
		    rwork[k] += s;
		}
	    }
	    s = 0.;
	    for (i = 0; i < A->nrow; ++i) {
		if (rwork[i] > safe2)
		    s = MAX( s, z_abs1(&work[i]) / rwork[i] );
		else
		    s = MAX( s, (z_abs1(&work[i]) + safe1) / 
				(rwork[i] + safe1) );
	    }
	    berr[j] = s;

	    /* Test stopping criterion. Continue iterating if   
	       1) The residual BERR(J) is larger than machine epsilon, and   
	       2) BERR(J) decreased by at least a factor of 2 during the   
	          last iteration, and   
	       3) At most ITMAX iterations tried. */

	    if (berr[j] > eps && berr[j] * 2. <= lstres && count < ITMAX) {
		/* Update solution and try again. */
		zgstrs (trans, L, U, perm_r, perm_c, &Bjcol, info);
		
#ifdef _CRAY
		CAXPY(&A->nrow, &done, work, &ione,
		       &Xmat[j*ldx], &ione);
#else
		zaxpy_(&A->nrow, &done, work, &ione,
		       &Xmat[j*ldx], &ione);
#endif
		lstres = berr[j];
		++count;
	    } else {
		break;
	    }
        
	} /* end while */

	/* Bound error from formula:
	   norm(X - XTRUE) / norm(X) .le. FERR = norm( abs(inv(op(A)))*   
	   ( abs(R) + NZ*EPS*( abs(op(A))*abs(X)+abs(B) ))) / norm(X)   
          where   
            norm(Z) is the magnitude of the largest component of Z   
            inv(op(A)) is the inverse of op(A)   
            abs(Z) is the componentwise absolute value of the matrix or
	       vector Z   
            NZ is the maximum number of nonzeros in any row of A, plus 1   
            EPS is machine epsilon   

          The i-th component of abs(R)+NZ*EPS*(abs(op(A))*abs(X)+abs(B))   
          is incremented by SAFE1 if the i-th component of   
          abs(op(A))*abs(X) + abs(B) is less than SAFE2.   

          Use ZLACON to estimate the infinity-norm of the matrix   
             inv(op(A)) * diag(W),   
          where W = abs(R) + NZ*EPS*( abs(op(A))*abs(X)+abs(B) ))) */
	
	for (i = 0; i < A->nrow; ++i) rwork[i] = z_abs1( &Bptr[i] );
	
	/* Compute abs(op(A))*abs(X) + abs(B). */
	if ( notran ) {
	    for (k = 0; k < A->ncol; ++k) {
		xk = z_abs1( &Xptr[k] );
		for (i = Astore->colptr[k]; i < Astore->colptr[k+1]; ++i)
		    rwork[Astore->rowind[i]] += z_abs1(&Aval[i]) * xk;
	    }
	} else {
	    for (k = 0; k < A->ncol; ++k) {
		s = 0.;
		for (i = Astore->colptr[k]; i < Astore->colptr[k+1]; ++i) {
		    irow = Astore->rowind[i];
		    xk = z_abs1( &Xptr[irow] );
		    s += z_abs1(&Aval[i]) * xk;
		}
		rwork[k] += s;
	    }
	}
	
	for (i = 0; i < A->nrow; ++i)
	    if (rwork[i] > safe2)
		rwork[i] = z_abs(&work[i]) + (iwork[i]+1)*eps*rwork[i];
	    else
		rwork[i] = z_abs(&work[i])+(iwork[i]+1)*eps*rwork[i]+safe1;
	kase = 0;

	do {
	    zlacon_(&A->nrow, &work[A->nrow], work,
		    &ferr[j], &kase);
	    if (kase == 0) break;

	    if (kase == 1) {
		/* Multiply by diag(W)*inv(op(A)**T)*(diag(C) or diag(R)). */
		if ( notran && colequ )
		    for (i = 0; i < A->ncol; ++i) {
		        zd_mult(&work[i], &work[i], C[i]);
	            }
		else if ( !notran && rowequ )
		    for (i = 0; i < A->nrow; ++i) {
		        zd_mult(&work[i], &work[i], R[i]);
                    }

		zgstrs (transt, L, U, perm_r, perm_c, &Bjcol, info);
		
		for (i = 0; i < A->nrow; ++i) {
		    zd_mult(&work[i], &work[i], rwork[i]);
	 	}
	    } else {
		/* Multiply by (diag(C) or diag(R))*inv(op(A))*diag(W). */
		for (i = 0; i < A->nrow; ++i) {
		    zd_mult(&work[i], &work[i], rwork[i]);
		}
		
		zgstrs (trans, L, U, perm_r, perm_c, &Bjcol, info);
		
		if ( notran && colequ )
		    for (i = 0; i < A->ncol; ++i) {
		        zd_mult(&work[i], &work[i], C[i]);
		    }
		else if ( !notran && rowequ )
		    for (i = 0; i < A->ncol; ++i) {
		        zd_mult(&work[i], &work[i], R[i]);  
		    }
	    }
	    
	} while ( kase != 0 );

	/* Normalize error. */
	lstres = 0.;
 	if ( notran && colequ ) {
	    for (i = 0; i < A->nrow; ++i)
	    	lstres = MAX( lstres, C[i] * z_abs1( &Xptr[i]) );
  	} else if ( !notran && rowequ ) {
	    for (i = 0; i < A->nrow; ++i)
	    	lstres = MAX( lstres, R[i] * z_abs1( &Xptr[i]) );
	} else {
	    for (i = 0; i < A->nrow; ++i)
	    	lstres = MAX( lstres, z_abs1( &Xptr[i]) );
	}
	if ( lstres != 0. )
	    ferr[j] /= lstres;

    } /* for each RHS j ... */
    
    SUPERLU_FREE(work);
    SUPERLU_FREE(rwork);
    SUPERLU_FREE(iwork);
    SUPERLU_FREE(Bjcol.Store);

    return;

} /* zgsrfs */