#pragma once

#include <Eigen/Sparse>
#include <string>

// GPU-accelerated sparse linear solver using NVIDIA cuSOLVER.
// Solves K*U = F where K is a symmetric positive-definite sparse matrix
// (stiffness matrix from FEA). Uses sparse Cholesky factorization on GPU.
//
// This is a drop-in replacement for Eigen's SimplicialLDLT path.
// The matrix K must be in Eigen's column-major CSC format (default).
// cuSOLVER uses CSR internally; we transpose at transfer time.

namespace cuda_solver {

// Returns true if a CUDA-capable GPU is available.
bool isGpuAvailable();

// Returns a human-readable string describing the GPU (name, memory, etc).
std::string getGpuInfo();

// Solves the sparse system K * U = F using cuSOLVER on GPU.
// K must be SPD (symmetric positive definite), as produced by FEA assembly.
// Returns true on success, false on failure.
// On success, U is resized and filled with the solution.
bool solveOnGpu(const Eigen::SparseMatrix<double>& K,
                const Eigen::VectorXd& F,
                Eigen::VectorXd& U);

} // namespace cuda_solver
