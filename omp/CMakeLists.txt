find_package(OpenMP REQUIRED)

add_library(ginkgo_omp $<TARGET_OBJECTS:ginkgo_omp_device> "")
target_sources(ginkgo_omp
    PRIVATE
    base/version.cpp
    components/fill_array.cpp
    components/precision_conversion.cpp
    components/prefix_sum.cpp
    factorization/ilu_kernels.cpp
    factorization/factorization_kernels.cpp
    factorization/par_ict_kernels.cpp
    factorization/par_ilu_kernels.cpp
    factorization/par_ilut_kernels.cpp
    matrix/coo_kernels.cpp
    matrix/csr_kernels.cpp
    matrix/dense_kernels.cpp
    matrix/diagonal_kernels.cpp
    matrix/ell_kernels.cpp
    matrix/hybrid_kernels.cpp
    matrix/sellp_kernels.cpp
    matrix/sparsity_csr_kernels.cpp
    preconditioner/isai_kernels.cpp
    preconditioner/jacobi_kernels.cpp
    solver/bicg_kernels.cpp
    solver/bicgstab_kernels.cpp
    solver/cg_kernels.cpp
    solver/cgs_kernels.cpp
    solver/fcg_kernels.cpp
    solver/gmres_kernels.cpp
    solver/ir_kernels.cpp
    solver/lower_trs_kernels.cpp
    solver/upper_trs_kernels.cpp
    stop/criterion_kernels.cpp
    stop/residual_norm_kernels.cpp)

ginkgo_compile_features(ginkgo_omp)

target_link_libraries(ginkgo_omp PRIVATE "${OpenMP_CXX_LIBRARIES}")
target_include_directories(ginkgo_omp PRIVATE "${OpenMP_CXX_INCLUDE_DIRS}")
# We first separate the arguments, otherwise, the target_compile_options adds it as a string
# and the compiler is unhappy with the quotation marks.
separate_arguments(OpenMP_SEP_FLAGS NATIVE_COMMAND "${OpenMP_CXX_FLAGS}")
target_compile_options(ginkgo_omp PRIVATE "${OpenMP_SEP_FLAGS}")
target_compile_options(ginkgo_omp PRIVATE "${GINKGO_COMPILER_FLAGS}")

# Need to link against ginkgo_cuda for the `raw_copy_to(CudaExecutor ...)` method
target_link_libraries(ginkgo_omp PUBLIC ginkgo_cuda)
# Need to link against ginkgo_hip for the `raw_copy_to(HipExecutor ...)` method
target_link_libraries(ginkgo_omp PUBLIC ginkgo_hip)

ginkgo_default_includes(ginkgo_omp)
ginkgo_install_library(ginkgo_omp omp)

if (GINKGO_CHECK_CIRCULAR_DEPS)
    ginkgo_check_headers(ginkgo_omp)
endif()

if(GINKGO_BUILD_TESTS)
    add_subdirectory(test)
endif()

# Propagate some useful information
set(OpenMP_CXX_VERSION ${OpenMP_CXX_VERSION} PARENT_SCOPE)
set(OpenMP_CXX_LIBRARIES ${OpenMP_CXX_LIBRARIES} PARENT_SCOPE)
