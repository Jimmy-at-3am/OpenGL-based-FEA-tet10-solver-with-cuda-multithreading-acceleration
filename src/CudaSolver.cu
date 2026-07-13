// ---------------------------------------------------------------------------
// CudaSolver.cu  –  GPU-accelerated iterative sparse solver
// ---------------------------------------------------------------------------
// Implements a Preconditioned Conjugate Gradient (PCG) solver on GPU using
// cuSPARSE for the sparse matrix-vector multiply (SpMV).
//
// WHY PCG INSTEAD OF SPARSE CHOLESKY:
//   Sparse Cholesky (cusolverSpDcsrlsvchol) creates a dense fill-in factor
//   during factorization — for a large 3D FEA stiffness matrix with 36M nnz
//   this can require 5-20x the input memory (1.5-6 GB), exceeding 4 GB VRAM.
//   PCG is purely iterative: it only needs O(nnz) VRAM at all times (just the
//   original matrix + a handful of n-length vectors). No fill-in ever occurs.
//
// PRECONDITIONER: Jacobi (diagonal scaling)
//   Extracts the diagonal of K, scales both K and F by 1/diag(K).
//   This is the standard choice for FEA penalty-BC matrices (where fixed-DOF
//   entries are huge), and mirrors Eigen's DiagonalPreconditioner.
//
// GPU operations: cuSPARSE SpMV + cuBLAS reductions + custom vector kernels.
// ---------------------------------------------------------------------------

#include "CudaSolver.h"

#include <cuda_runtime.h>
#include <cusparse.h>
#include <cublas_v2.h>

#include <iostream>
#include <vector>
#include <cmath>
#include <chrono>
#include <algorithm>
#include <limits>

// ---------------------------------------------------------------------------
// Error check macros (clean up on failure)
// ---------------------------------------------------------------------------
#define CUDA_CHECK_GOTO(call, label)                                            \
    do {                                                                        \
        cudaError_t _e = (call);                                                \
        if (_e != cudaSuccess) {                                                \
            std::cerr << "[GPU] CUDA error: " << cudaGetErrorString(_e)         \
                      << " at " << __FILE__ << ":" << __LINE__ << std::endl;    \
            goto label;                                                         \
        }                                                                       \
    } while (0)

#define CUSPARSE_CHECK_GOTO(call, label)                                        \
    do {                                                                        \
        cusparseStatus_t _s = (call);                                           \
        if (_s != CUSPARSE_STATUS_SUCCESS) {                                    \
            std::cerr << "[GPU] cuSPARSE error " << (int)_s                     \
                      << " at " << __FILE__ << ":" << __LINE__ << std::endl;    \
            goto label;                                                         \
        }                                                                       \
    } while (0)

#define CUBLAS_CHECK_GOTO(call, label)                                          \
    do {                                                                        \
        cublasStatus_t _s = (call);                                             \
        if (_s != CUBLAS_STATUS_SUCCESS) {                                      \
            std::cerr << "[GPU] cuBLAS error " << (int)_s                       \
                      << " at " << __FILE__ << ":" << __LINE__ << std::endl;    \
            goto label;                                                         \
        }                                                                       \
    } while (0)

// ---------------------------------------------------------------------------
// CUDA kernels for per-element vector operations
// ---------------------------------------------------------------------------

// Apply Jacobi preconditioner:  z[i] = r[i] / diag[i]
__global__ void jacobiApply(const double* __restrict__ diag,
                             const double* __restrict__ r,
                             double* __restrict__ z,
                             int n) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) z[i] = r[i] / diag[i];
}

// Device kernels to compute alpha and beta on GPU without host sync
__global__ void computeAlphaDevice(const double* rz, const double* pAp,
                                   double* alpha, double* minus_alpha,
                                   int* breakdown) {
    const double numerator   = *rz;
    const double denominator = *pAp;
    if (!isfinite(numerator) || !isfinite(denominator) || denominator <= 0.0) {
        *alpha = 0.0;
        *minus_alpha = 0.0;
        atomicExch(breakdown, 1);
        return;
    }
    double a = numerator / denominator;
    if (!isfinite(a)) {
        a = 0.0;
        atomicExch(breakdown, 1);
    }
    *alpha = a;
    *minus_alpha = -a;
}

__global__ void computeBetaDevice(double* rz, const double* rz_new,
                                  double* beta, int* breakdown) {
    const double previous = *rz;
    const double next     = *rz_new;
    if (!isfinite(previous) || !isfinite(next) || previous <= 0.0 || next < 0.0) {
        *beta = 0.0;
        atomicExch(breakdown, 1);
        return;
    }
    const double value = next / previous;
    if (!isfinite(value)) {
        *beta = 0.0;
        atomicExch(breakdown, 1);
        return;
    }
    *beta = value;
    *rz = next;
}

// Element-wise vector AXPY: y[i] += alpha * x[i]  (scalar alpha on device pointer)
__global__ void vecAxpyDevicePtr(const double* __restrict__ alpha, const double* __restrict__ x, double* __restrict__ y, int n) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    double a = *alpha;
    if (i < n) y[i] += a * x[i];
}

// y[i] = x[i] + beta * y[i] (scalar beta on device pointer)
__global__ void vecXpBetaYDevicePtr(const double* __restrict__ x, const double* __restrict__ beta, double* __restrict__ y, int n) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    double b = *beta;
    if (i < n) y[i] = x[i] + b * y[i];
}

// Extract diagonal of CSR matrix into diag[i]
__global__ void extractDiagonal(const int* __restrict__ rowPtr,
                                 const int* __restrict__ colIdx,
                                 const double* __restrict__ vals,
                                 double* __restrict__ diag,
                                 int* invalidDiagonal, int n) {
    int row = blockIdx.x * blockDim.x + threadIdx.x;
    if (row >= n) return;
    double value = 0.0;
    bool found = false;
    for (int j = rowPtr[row]; j < rowPtr[row + 1]; ++j) {
        if (colIdx[j] == row) {
            value = vals[j];
            found = true;
            break;
        }
    }
    if (!found || !isfinite(value) || value <= 0.0) {
        diag[row] = 1.0; // keep later kernels defined while the host aborts
        atomicExch(invalidDiagonal, 1);
        return;
    }
    diag[row] = value;
}

namespace cuda_solver {

// ---------------------------------------------------------------------------
bool isGpuAvailable() {
    int deviceCount = 0;
    cudaError_t err = cudaGetDeviceCount(&deviceCount);
    return (err == cudaSuccess && deviceCount > 0);
}

std::string getGpuInfo() {
    int deviceCount = 0;
    if (cudaGetDeviceCount(&deviceCount) != cudaSuccess || deviceCount == 0)
        return "No CUDA GPU detected";
    cudaDeviceProp prop;
    cudaGetDeviceProperties(&prop, 0);
    char buf[512];
    snprintf(buf, sizeof(buf), "%s (SM %d.%d, %.0f MB VRAM, %d SMs)",
             prop.name, prop.major, prop.minor,
             prop.totalGlobalMem / (1024.0 * 1024.0),
             prop.multiProcessorCount);
    return std::string(buf);
}

// ---------------------------------------------------------------------------
// solveOnGpu  –  Jacobi-Preconditioned Conjugate Gradient on GPU
// ---------------------------------------------------------------------------
// Memory usage: ~(nnz * 12 + n * 64) bytes. For nnz=36M, n=458K:
//   matrix data (vals+cols+rows) = ~400 MB
//   7 working vectors @ 8 bytes * n = ~26 MB
//   total ≈ 426 MB  <<  4096 MB VRAM.  No fill-in ever allocated.
// ---------------------------------------------------------------------------
bool solveOnGpu(const CsrMatrix& K,
                const Eigen::VectorXd& F,
                Eigen::VectorXd& U) {

    auto t0 = std::chrono::high_resolution_clock::now();

    if (K.rows() == 0 || K.nonZeros() == 0) {
        std::cerr << "[GPU] Empty matrix." << std::endl;
        return false;
    }
    if (K.rows() != K.cols() || F.size() != K.rows()) {
        std::cerr << "[GPU] Invalid system dimensions: K=" << K.rows() << "x"
                  << K.cols() << ", F=" << F.size() << std::endl;
        return false;
    }
    if (!K.isCompressed()) {
        std::cerr << "[GPU] Matrix must be compressed before passing its CSR arrays."
                  << std::endl;
        return false;
    }
    if (K.rows() > std::numeric_limits<int>::max() ||
        K.nonZeros() > std::numeric_limits<int>::max()) {
        std::cerr << "[GPU] Matrix exceeds the solver's 32-bit CSR index range."
                  << std::endl;
        return false;
    }
    if (!F.allFinite()) {
        std::cerr << "[GPU] Right-hand side contains NaN or infinity." << std::endl;
        return false;
    }

    const int n   = static_cast<int>(K.rows());
    const int nnz = static_cast<int>(K.nonZeros());
    for (int i = 0; i < nnz; ++i) {
        if (!std::isfinite(K.valuePtr()[i])) {
            std::cerr << "[GPU] Stiffness matrix contains NaN or infinity." << std::endl;
            return false;
        }
    }
    if (F.isZero(0.0)) {
        U.setZero(n);
        std::cout << "[GPU] Zero right-hand side; returning the zero solution."
                  << std::endl;
        return true;
    }
    if (!isGpuAvailable()) {
        std::cerr << "[GPU] No usable CUDA device is available." << std::endl;
        return false;
    }

    const double CG_TOL = 1e-9;
    // In exact arithmetic CG needs at most n iterations, so the cap must not
    // sit BELOW n: the CPU MT path on the regression models already needs
    // ~9.9k Jacobi-PCG iterations, and a min(n, 10000) cap made comparable
    // GPU solves fail spuriously and fall through to a second (CPU) solve.
    // max(n, 10000) keeps the run finite while matching the CPU path's
    // max(nDOFs, 5000) convention; non-convergence is still reported honestly.
    const int CG_MAX_ITER = std::max(n, 10000);

    // CG working vectors on GPU:
    // r = residual, z = preconditioned residual, p = search direction
    // Ap = A*p, diag = Jacobi preconditioner
    double* d_r    = nullptr; // residual
    double* d_z    = nullptr; // M^-1 * r
    double* d_p    = nullptr; // search direction
    double* d_Ap   = nullptr; // A * p
    double* d_x    = nullptr; // solution
    double* d_b    = nullptr; // RHS
    double* d_diag = nullptr; // Jacobi preconditioner (diagonal)

    // Device scalars for fully fused CG loop
    double* d_rz          = nullptr;
    double* d_rz_new      = nullptr;
    double* d_pAp         = nullptr;
    double* d_alpha       = nullptr;
    double* d_minus_alpha = nullptr;
    double* d_beta        = nullptr;
    int*    d_invalidDiag = nullptr;
    int*    d_breakdown   = nullptr;

    int*    d_rowPtr = nullptr;
    int*    d_colIdx = nullptr;
    double* d_vals   = nullptr;

    cusparseHandle_t  spHandle  = nullptr;
    cublasHandle_t    blHandle  = nullptr;
    cusparseSpMatDescr_t matA   = nullptr;
    cusparseDnVecDescr_t vecP   = nullptr;
    cusparseDnVecDescr_t vecAp  = nullptr;
    void*             spBuf     = nullptr;

    bool success = false;
    double bNorm = 0.0;
    double target_rNorm = 0.0;
    std::chrono::high_resolution_clock::time_point tSolve0;
    int iter = 0;
    int blocks = (n + 255) / 256;
    bool converged = false;
    double finalRNorm = 0.0;

    // ------------------------------------------------------------------
    // 1. Allocate GPU memory
    // ------------------------------------------------------------------
    CUDA_CHECK_GOTO(cudaMalloc(&d_rowPtr, (n + 1) * sizeof(int)),    fail);
    CUDA_CHECK_GOTO(cudaMalloc(&d_colIdx, nnz     * sizeof(int)),    fail);
    CUDA_CHECK_GOTO(cudaMalloc(&d_vals,   nnz     * sizeof(double)), fail);
    CUDA_CHECK_GOTO(cudaMalloc(&d_b,      n       * sizeof(double)), fail);
    CUDA_CHECK_GOTO(cudaMalloc(&d_x,      n       * sizeof(double)), fail);
    CUDA_CHECK_GOTO(cudaMalloc(&d_r,      n       * sizeof(double)), fail);
    CUDA_CHECK_GOTO(cudaMalloc(&d_z,      n       * sizeof(double)), fail);
    CUDA_CHECK_GOTO(cudaMalloc(&d_p,      n       * sizeof(double)), fail);
    CUDA_CHECK_GOTO(cudaMalloc(&d_Ap,     n       * sizeof(double)), fail);
    CUDA_CHECK_GOTO(cudaMalloc(&d_diag,   n       * sizeof(double)), fail);

    CUDA_CHECK_GOTO(cudaMalloc(&d_rz,          sizeof(double)), fail);
    CUDA_CHECK_GOTO(cudaMalloc(&d_rz_new,      sizeof(double)), fail);
    CUDA_CHECK_GOTO(cudaMalloc(&d_pAp,         sizeof(double)), fail);
    CUDA_CHECK_GOTO(cudaMalloc(&d_alpha,       sizeof(double)), fail);
    CUDA_CHECK_GOTO(cudaMalloc(&d_minus_alpha, sizeof(double)), fail);
    CUDA_CHECK_GOTO(cudaMalloc(&d_beta,        sizeof(double)), fail);
    CUDA_CHECK_GOTO(cudaMalloc(&d_invalidDiag, sizeof(int)),    fail);
    CUDA_CHECK_GOTO(cudaMalloc(&d_breakdown,   sizeof(int)),    fail);

    // ------------------------------------------------------------------
    // 2. Host → Device transfer
    // ------------------------------------------------------------------
    CUDA_CHECK_GOTO(cudaMemcpy(d_rowPtr, K.outerIndexPtr(), (n + 1) * sizeof(int),    cudaMemcpyHostToDevice), fail);
    CUDA_CHECK_GOTO(cudaMemcpy(d_colIdx, K.innerIndexPtr(), nnz     * sizeof(int),    cudaMemcpyHostToDevice), fail);
    CUDA_CHECK_GOTO(cudaMemcpy(d_vals,   K.valuePtr(),      nnz     * sizeof(double), cudaMemcpyHostToDevice), fail);
    CUDA_CHECK_GOTO(cudaMemcpy(d_b,      F.data(),          n       * sizeof(double), cudaMemcpyHostToDevice), fail);
    CUDA_CHECK_GOTO(cudaMemset(d_x,      0,                 n       * sizeof(double)), fail); // x0 = 0
    CUDA_CHECK_GOTO(cudaMemset(d_invalidDiag, 0, sizeof(int)), fail);
    CUDA_CHECK_GOTO(cudaMemset(d_breakdown,   0, sizeof(int)), fail);

    {
        auto tTr = std::chrono::high_resolution_clock::now();
        double trMs = std::chrono::duration<double, std::milli>(tTr - t0).count();
        std::cout << "[GPU] Transfer to device: " << trMs << " ms"
                  << "  (n=" << n << ", nnz=" << nnz << ")" << std::endl;

        // Estimate VRAM usage
        size_t used = (size_t)(n + 1) * sizeof(int)
                    + (size_t)nnz     * sizeof(int)
                    + (size_t)nnz     * sizeof(double)
                    + (size_t)n       * sizeof(double) * 7;
        std::cout << "[GPU] Estimated VRAM usage: " << used / (1024 * 1024) << " MB"
                  << " (no fill-in — iterative PCG)" << std::endl;
    }

    // ------------------------------------------------------------------
    // 3. Extract Jacobi preconditioner diagonal
    // ------------------------------------------------------------------
    {
        int blocks = (n + 255) / 256;
        extractDiagonal<<<blocks, 256>>>(d_rowPtr, d_colIdx, d_vals, d_diag,
                                         d_invalidDiag, n);
        CUDA_CHECK_GOTO(cudaGetLastError(), fail);
        int invalidDiagonal = 0;
        CUDA_CHECK_GOTO(cudaMemcpy(&invalidDiagonal, d_invalidDiag, sizeof(int),
                                   cudaMemcpyDeviceToHost), fail);
        if (invalidDiagonal != 0) {
            std::cerr << "[GPU] Jacobi preconditioner requires a finite, positive "
                         "diagonal (the matrix is not SPD)." << std::endl;
            goto fail;
        }
    }

    // ------------------------------------------------------------------
    // 4. Create cuSPARSE handles and descriptors for SpMV: Ap = K * p
    // ------------------------------------------------------------------
    CUSPARSE_CHECK_GOTO(cusparseCreate(&spHandle), fail);
    CUBLAS_CHECK_GOTO(cublasCreate(&blHandle), fail);

    // CSR matrix descriptor (note: Eigen CSC == CSR for symmetric K)
    CUSPARSE_CHECK_GOTO(cusparseCreateCsr(
        &matA, n, n, nnz,
        d_rowPtr, d_colIdx, d_vals,
        CUSPARSE_INDEX_32I, CUSPARSE_INDEX_32I,
        CUSPARSE_INDEX_BASE_ZERO, CUDA_R_64F), fail);

    CUSPARSE_CHECK_GOTO(cusparseCreateDnVec(&vecP,  n, d_p,  CUDA_R_64F), fail);
    CUSPARSE_CHECK_GOTO(cusparseCreateDnVec(&vecAp, n, d_Ap, CUDA_R_64F), fail);

    // Query SpMV buffer size
    {
        double alpha = 1.0, beta = 0.0;
        size_t bufSize = 0;
        CUSPARSE_CHECK_GOTO(cusparseSpMV_bufferSize(
            spHandle, CUSPARSE_OPERATION_NON_TRANSPOSE,
            &alpha, matA, vecP, &beta, vecAp,
            CUDA_R_64F, CUSPARSE_SPMV_ALG_DEFAULT, &bufSize), fail);
        if (bufSize > 0)
            CUDA_CHECK_GOTO(cudaMalloc(&spBuf, bufSize), fail);

        // NVIDIA recommends preprocessing when the same CSR matrix is used by
        // repeated SpMV calls, exactly as in this CG loop.  The API was added
        // in CUDA 12.6; retain build compatibility with older toolkits.
#if defined(CUDART_VERSION) && CUDART_VERSION >= 12060
        CUSPARSE_CHECK_GOTO(cusparseSpMV_preprocess(
            spHandle, CUSPARSE_OPERATION_NON_TRANSPOSE,
            &alpha, matA, vecP, &beta, vecAp,
            CUDA_R_64F, CUSPARSE_SPMV_ALG_DEFAULT, spBuf), fail);
#endif
    }

    // ------------------------------------------------------------------
    // 5. PCG iterations
    // ------------------------------------------------------------------
    // Init: r = b - A*x0 = b  (since x0 = 0)
    CUDA_CHECK_GOTO(cudaMemcpy(d_r, d_b, n * sizeof(double), cudaMemcpyDeviceToDevice), fail);

    // Initial bNorm for convergence check
    CUBLAS_CHECK_GOTO(cublasDnrm2(blHandle, n, d_b, 1, &bNorm), fail);
    if (!std::isfinite(bNorm)) {
        std::cerr << "[GPU] Right-hand-side norm overflowed." << std::endl;
        goto fail;
    }
    target_rNorm = CG_TOL * bNorm;
    std::cout << "[GPU] CG starting. ||b||=" << bNorm << " target ||r|| < " << target_rNorm << std::endl;

    // Switch to DEVICE pointer mode so scalar results from cuBLAS stay on GPU
    CUBLAS_CHECK_GOTO(cublasSetPointerMode(blHandle, CUBLAS_POINTER_MODE_DEVICE), fail);

    // z = M^-1 * r  (Jacobi)
    {
        int blocks = (n + 255) / 256;
        jacobiApply<<<blocks, 256>>>(d_diag, d_r, d_z, n);
        CUDA_CHECK_GOTO(cudaGetLastError(), fail);
    }

    // p = z
    CUDA_CHECK_GOTO(cudaMemcpy(d_p, d_z, n * sizeof(double), cudaMemcpyDeviceToDevice), fail);

    // rz = r · z (result stays on device in d_rz)
    CUBLAS_CHECK_GOTO(cublasDdot(blHandle, n, d_r, 1, d_z, 1, d_rz), fail);

    tSolve0 = std::chrono::high_resolution_clock::now();
    finalRNorm = bNorm;

    for (; iter < CG_MAX_ITER; ++iter) {
        // Ap = A * p
        {
            double alpha = 1.0, beta = 0.0;
            // cusparseSpMV expects host pointers for alpha/beta because its pointer mode is HOST by default
            CUSPARSE_CHECK_GOTO(cusparseSpMV(
                spHandle, CUSPARSE_OPERATION_NON_TRANSPOSE,
                &alpha, matA, vecP, &beta, vecAp,
                CUDA_R_64F, CUSPARSE_SPMV_ALG_DEFAULT, spBuf), fail);
        }

        // pAp = p · Ap (device pointer d_pAp)
        CUBLAS_CHECK_GOTO(cublasDdot(blHandle, n, d_p, 1, d_Ap, 1, d_pAp), fail);

        // alpha_cg = rz / (p · Ap)  (done on device)
        computeAlphaDevice<<<1, 1>>>(d_rz, d_pAp, d_alpha, d_minus_alpha,
                                     d_breakdown);
        CUDA_CHECK_GOTO(cudaGetLastError(), fail);

        // x = x + alpha * p
        vecAxpyDevicePtr<<<blocks, 256>>>(d_alpha, d_p, d_x, n);
        CUDA_CHECK_GOTO(cudaGetLastError(), fail);

        // r = r - alpha * Ap
        vecAxpyDevicePtr<<<blocks, 256>>>(d_minus_alpha, d_Ap, d_r, n);
        CUDA_CHECK_GOTO(cudaGetLastError(), fail);

        // Check convergence periodically (every 50 iterations) to avoid pipeline stalls
        if ((iter + 1) % 50 == 0) {
            CUBLAS_CHECK_GOTO(cublasSetPointerMode(blHandle, CUBLAS_POINTER_MODE_HOST), fail);
            double rNorm = 0.0;
            CUBLAS_CHECK_GOTO(cublasDnrm2(blHandle, n, d_r, 1, &rNorm), fail);
            finalRNorm = rNorm;

            if (std::isfinite(rNorm) && rNorm <= target_rNorm) {
                iter++;
                converged = true;
                std::cout << "[GPU] CG converged at iter " << iter << "  ||r||=" << rNorm << std::endl;
                break;
            }

            int breakdown = 0;
            CUDA_CHECK_GOTO(cudaMemcpy(&breakdown, d_breakdown, sizeof(int),
                                       cudaMemcpyDeviceToHost), fail);
            if (!std::isfinite(rNorm) || breakdown != 0) {
                std::cerr << "[GPU] CG numerical breakdown at iter " << (iter + 1)
                          << " (||r||=" << rNorm << ")." << std::endl;
                break;
            }

            if ((iter + 1) % 500 == 0) {
                std::cout << "[GPU] CG iter " << (iter + 1) << "  ||r||=" << rNorm << std::endl;
            }
            CUBLAS_CHECK_GOTO(cublasSetPointerMode(blHandle, CUBLAS_POINTER_MODE_DEVICE), fail);
        }

        // z = M^-1 * r
        jacobiApply<<<blocks, 256>>>(d_diag, d_r, d_z, n);
        CUDA_CHECK_GOTO(cudaGetLastError(), fail);

        // rz_new = r * z (device pointer d_rz_new)
        CUBLAS_CHECK_GOTO(cublasDdot(blHandle, n, d_r, 1, d_z, 1, d_rz_new), fail);

        // beta = rz_new / rz, rz = rz_new
        computeBetaDevice<<<1, 1>>>(d_rz, d_rz_new, d_beta, d_breakdown);
        CUDA_CHECK_GOTO(cudaGetLastError(), fail);

        // p = z + beta * p
        vecXpBetaYDevicePtr<<<blocks, 256>>>(d_z, d_beta, d_p, n);
        CUDA_CHECK_GOTO(cudaGetLastError(), fail);
    }

    CUDA_CHECK_GOTO(cudaDeviceSynchronize(), fail);

    // The loop may end between periodic checks or by reaching the iteration
    // cap.  Never return an unconverged vector as a successful FEA solution.
    if (!converged) {
        CUBLAS_CHECK_GOTO(cublasSetPointerMode(blHandle, CUBLAS_POINTER_MODE_HOST), fail);
        CUBLAS_CHECK_GOTO(cublasDnrm2(blHandle, n, d_r, 1, &finalRNorm), fail);
        converged = std::isfinite(finalRNorm) && finalRNorm <= target_rNorm;
    }

    {
        auto tSolve1 = std::chrono::high_resolution_clock::now();
        double solveMs = std::chrono::duration<double, std::milli>(tSolve1 - tSolve0).count();
        std::cout << "[GPU] PCG total: " << iter << " iters  "
                  << solveMs << " ms  ("
                  << (solveMs / std::max(iter, 1)) << " ms/iter)" << std::endl;
    }

    if (!converged) {
        std::cerr << "[GPU] CG did not converge after " << iter
                  << " iterations (||r||=" << finalRNorm
                  << ", target=" << target_rNorm << ")." << std::endl;
        goto fail;
    }

    // ------------------------------------------------------------------
    // 6. Device → Host: copy solution
    // ------------------------------------------------------------------
    U.resize(n);
    CUDA_CHECK_GOTO(cudaMemcpy(U.data(), d_x, n * sizeof(double), cudaMemcpyDeviceToHost), fail);

    {
        auto tEnd = std::chrono::high_resolution_clock::now();
        double totalMs = std::chrono::duration<double, std::milli>(tEnd - t0).count();
        std::cout << "[GPU] Total GPU time: " << totalMs << " ms" << std::endl;
    }

    success = true;

fail:
    // ------------------------------------------------------------------
    // 7. Cleanup (always runs)
    // ------------------------------------------------------------------
    // Descriptors are destroyed before their backing allocations.
    if (vecP)     cusparseDestroyDnVec(vecP);
    if (vecAp)    cusparseDestroyDnVec(vecAp);
    if (matA)     cusparseDestroySpMat(matA);
    if (spHandle) cusparseDestroy(spHandle);
    if (blHandle) cublasDestroy(blHandle);
    if (spBuf)   cudaFree(spBuf);
    if (d_rowPtr) cudaFree(d_rowPtr);
    if (d_colIdx) cudaFree(d_colIdx);
    if (d_vals)   cudaFree(d_vals);
    if (d_b)      cudaFree(d_b);
    if (d_x)      cudaFree(d_x);
    if (d_r)      cudaFree(d_r);
    if (d_z)      cudaFree(d_z);
    if (d_p)      cudaFree(d_p);
    if (d_Ap)     cudaFree(d_Ap);
    if (d_diag)   cudaFree(d_diag);
    if (d_invalidDiag) cudaFree(d_invalidDiag);
    if (d_breakdown)   cudaFree(d_breakdown);

    return success;
}

} // namespace cuda_solver
