#pragma once

#include <Eigen/Sparse>
#include <atomic>
#include <string>

// GPU-accelerated sparse linear solver using NVIDIA cuBLAS and cuSPARSE.
// Solves K*U = F where K is a symmetric positive-definite sparse matrix
// (stiffness matrix from FEA). Uses Jacobi-preconditioned conjugate gradient.
//
// This is a drop-in replacement for Eigen's SimplicialLDLT path.
// The matrix K must use the row-major CsrMatrix type below and be compressed
// before solveOnGpu is called.

namespace cuda_solver {

// Explicit CSR storage contract for cuSPARSE. Keeping this type shared between
// the caller and implementation prevents accidental CSC-as-CSR reinterpretation.
using CsrMatrix = Eigen::SparseMatrix<double, Eigen::RowMajor, int>;

// Returns true if a CUDA-capable GPU is available.
bool isGpuAvailable();

// Returns a human-readable string describing the GPU (name, memory, etc).
std::string getGpuInfo();

// Solves the sparse system K * U = F using PCG on the GPU.
// K must be SPD (symmetric positive definite), as produced by FEA assembly.
// Returns true on success, false on failure.
// On success, U is resized and filled with the solution.
//
// `cancel` is optional. Unlike Eigen's CG (whose solve() is a single opaque
// call), this PCG loop is hand-written, so the flag is polled at the periodic
// convergence checkpoint and the solve aborts there — cancelling a GPU solve
// costs at most ~50 iterations, not the whole run. A cancelled solve returns
// false and leaves U untouched.
bool solveOnGpu(const CsrMatrix& K,
                const Eigen::VectorXd& F,
                Eigen::VectorXd& U,
                int statusDepth = 2,
                const std::atomic<bool>* cancel = nullptr);

} // namespace cuda_solver
