/*******************************<GINKGO LICENSE>******************************
Copyright (c) 2017-2020, the Ginkgo authors
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

1. Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
contributors may be used to endorse or promote products derived from
this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
******************************<GINKGO LICENSE>*******************************/

#include <ginkgo/core/solver/idr.hpp>


#include <iostream>
#include <random>


#include <gtest/gtest.h>


#include <ginkgo/core/base/array.hpp>
#include <ginkgo/core/base/exception.hpp>
#include <ginkgo/core/base/executor.hpp>
#include <ginkgo/core/matrix/dense.hpp>
#include <ginkgo/core/stop/combined.hpp>
#include <ginkgo/core/stop/iteration.hpp>
#include <ginkgo/core/stop/residual_norm.hpp>


#include "core/solver/idr_kernels.hpp"
#include "core/test/utils.hpp"


namespace {


class Idr : public ::testing::Test {
protected:
    using Mtx = gko::matrix::Dense<>;
    using Solver = gko::solver::Idr<>;

    Idr() : rand_engine(30) {}

    void SetUp()
    {
        ref = gko::ReferenceExecutor::create();
        omp = gko::OmpExecutor::create();

        mtx = gen_mtx(123, 123);
        make_diag_dominant(mtx.get());
        d_mtx = Mtx::create(omp);
        d_mtx->copy_from(mtx.get());
        omp_idr_factory =
            Solver::build()
                .with_criteria(
                    gko::stop::Iteration::build().with_max_iters(246u).on(omp),
                    gko::stop::ResidualNormReduction<>::build()
                        .with_reduction_factor(1e-15)
                        .on(omp))
                .on(omp);

        ref_idr_factory =
            Solver::build()
                .with_criteria(
                    gko::stop::Iteration::build().with_max_iters(246u).on(ref),
                    gko::stop::ResidualNormReduction<>::build()
                        .with_reduction_factor(1e-15)
                        .on(ref))
                .on(ref);
    }

    void TearDown()
    {
        if (omp != nullptr) {
            ASSERT_NO_THROW(omp->synchronize());
        }
    }

    std::unique_ptr<Mtx> gen_mtx(int num_rows, int num_cols)
    {
        return gko::test::generate_random_matrix<Mtx>(
            num_rows, num_cols,
            std::uniform_int_distribution<>(num_cols, num_cols),
            std::normal_distribution<>(0.0, 1.0), rand_engine, ref);
    }

    void initialize_data()
    {
        int size = 597;
        int nrhs = 17;
        int s = 2;
        x = gen_mtx(size, nrhs);
        b = gen_mtx(size, nrhs);
        r = gen_mtx(size, nrhs);
        m = gen_mtx(s, nrhs * s);
        f = gen_mtx(s, nrhs);
        g = gen_mtx(size, nrhs * s);
        u = gen_mtx(size, nrhs * s);
        c = gen_mtx(s, nrhs);
        v = gen_mtx(size, nrhs);
        p = gen_mtx(s, size);
        omega = gen_mtx(1, nrhs);
        tht = gen_mtx(1, nrhs);
        t_norm = gen_mtx(1, nrhs);
        residual_norm = gen_mtx(1, nrhs);
        rho = gen_mtx(1, nrhs);
        stop_status = std::unique_ptr<gko::Array<gko::stopping_status>>(
            new gko::Array<gko::stopping_status>(ref, nrhs));
        for (size_t i = 0; i < nrhs; ++i) {
            stop_status->get_data()[i].reset();
        }

        d_x = Mtx::create(omp);
        d_b = Mtx::create(omp);
        d_r = Mtx::create(omp);
        d_m = Mtx::create(omp);
        d_f = Mtx::create(omp);
        d_g = Mtx::create(omp);
        d_u = Mtx::create(omp);
        d_c = Mtx::create(omp);
        d_v = Mtx::create(omp);
        d_p = Mtx::create(omp);
        d_omega = Mtx::create(omp);
        d_tht = Mtx::create(omp);
        d_t_norm = Mtx::create(omp);
        d_residual_norm = Mtx::create(omp);
        d_rho = Mtx::create(omp);
        d_stop_status = std::unique_ptr<gko::Array<gko::stopping_status>>(
            new gko::Array<gko::stopping_status>(omp));

        d_x->copy_from(x.get());
        d_b->copy_from(b.get());
        d_r->copy_from(r.get());
        d_m->copy_from(m.get());
        d_f->copy_from(f.get());
        d_g->copy_from(g.get());
        d_u->copy_from(u.get());
        d_c->copy_from(c.get());
        d_v->copy_from(v.get());
        d_p->copy_from(p.get());
        d_omega->copy_from(omega.get());
        d_tht->copy_from(tht.get());
        d_t_norm->copy_from(t_norm.get());
        d_residual_norm->copy_from(residual_norm.get());
        d_rho->copy_from(rho.get());
        *d_stop_status =
            *stop_status;  // copy_from is not a public member function of Array
    }

    void make_diag_dominant(Mtx *mtx)
    {
        using std::abs;
        for (int i = 0; i < mtx->get_size()[0]; ++i) {
            auto sum = gko::zero<Mtx::value_type>();
            for (int j = 0; j < mtx->get_size()[1]; ++j) {
                sum += abs(mtx->at(i, j));
            }
            mtx->at(i, i) = sum;
        }
    }

    std::shared_ptr<gko::ReferenceExecutor> ref;
    std::shared_ptr<const gko::OmpExecutor> omp;

    std::ranlux48 rand_engine;

    std::shared_ptr<Mtx> mtx;
    std::shared_ptr<Mtx> d_mtx;
    std::unique_ptr<Solver::Factory> omp_idr_factory;
    std::unique_ptr<Solver::Factory> ref_idr_factory;

    std::unique_ptr<Mtx> x;
    std::unique_ptr<Mtx> b;
    std::unique_ptr<Mtx> r;
    std::unique_ptr<Mtx> m;
    std::unique_ptr<Mtx> f;
    std::unique_ptr<Mtx> g;
    std::unique_ptr<Mtx> u;
    std::unique_ptr<Mtx> c;
    std::unique_ptr<Mtx> v;
    std::unique_ptr<Mtx> p;
    std::unique_ptr<Mtx> omega;
    std::unique_ptr<Mtx> tht;
    std::unique_ptr<Mtx> t_norm;
    std::unique_ptr<Mtx> residual_norm;
    std::unique_ptr<Mtx> rho;
    std::unique_ptr<gko::Array<gko::stopping_status>> stop_status;

    std::unique_ptr<Mtx> d_x;
    std::unique_ptr<Mtx> d_b;
    std::unique_ptr<Mtx> d_r;
    std::unique_ptr<Mtx> d_m;
    std::unique_ptr<Mtx> d_f;
    std::unique_ptr<Mtx> d_g;
    std::unique_ptr<Mtx> d_u;
    std::unique_ptr<Mtx> d_c;
    std::unique_ptr<Mtx> d_v;
    std::unique_ptr<Mtx> d_p;
    std::unique_ptr<Mtx> d_omega;
    std::unique_ptr<Mtx> d_tht;
    std::unique_ptr<Mtx> d_t_norm;
    std::unique_ptr<Mtx> d_residual_norm;
    std::unique_ptr<Mtx> d_rho;
    std::unique_ptr<gko::Array<gko::stopping_status>> d_stop_status;
};


TEST_F(Idr, OmpIdrInitializeIsEquivalentToRef)
{
    initialize_data();

    gko::kernels::reference::idr::initialize(ref, m.get(), stop_status.get());
    gko::kernels::omp::idr::initialize(omp, d_m.get(), d_stop_status.get());

    GKO_ASSERT_MTX_NEAR(m, d_m, 1e-14)
}


TEST_F(Idr, OmpIdrStep1IsEquivalentToRef)
{
    initialize_data();

    gko::size_type k = 2;
    gko::kernels::reference::idr::step_1(ref, k, m.get(), f.get(), r.get(),
                                         g.get(), c.get(), v.get(),
                                         stop_status.get());
    gko::kernels::omp::idr::step_1(omp, k, d_m.get(), d_f.get(), d_r.get(),
                                   d_g.get(), d_c.get(), d_v.get(),
                                   d_stop_status.get());

    GKO_ASSERT_MTX_NEAR(c, d_c, 1e-14);
    GKO_ASSERT_MTX_NEAR(v, d_v, 1e-14);
}


/*TEST_F(Idr, OmpIdrStep2IsEquivalentToRef)
{
    initialize_data();

    gko::size_type k = 2;
    gko::kernels::reference::idr::step_2(ref, k, omega.get(), v.get(), c.get(),
                                              u.get(), stop_status.get());
    gko::kernels::omp::idr::step_2(omp, k, d_omega.get(), d_v.get(), d_c.get(),
                                        d_u.get(), d_stop_status.get());

    GKO_ASSERT_MTX_NEAR(u, d_u, 1e-14);
}*/


TEST_F(Idr, OmpIdrStep3IsEquivalentToRef)
GKO_NOT_IMPLEMENTED;
//{
// TODO (script:idr): change the code imported from solver/bicgstab if needed
//    initialize_data();
//
//    gko::kernels::reference::idr::step_3(
//        ref, x.get(), r.get(), s.get(), t.get(), y.get(), z.get(),
//        alpha.get(), beta.get(), gamma.get(), omega.get(), stop_status.get());
//    gko::kernels::omp::idr::step_3(
//        omp, d_x.get(), d_r.get(), d_s.get(), d_t.get(), d_y.get(), d_z.get(),
//        d_alpha.get(), d_beta.get(), d_gamma.get(), d_omega.get(),
//        d_stop_status.get());
//
//    GKO_ASSERT_MTX_NEAR(d_omega, omega, 1e-14);
//    GKO_ASSERT_MTX_NEAR(d_x, x, 1e-14);
//    GKO_ASSERT_MTX_NEAR(d_r, r, 1e-14);
//}


TEST_F(Idr, OmpIdrApplyOneRHSIsEquivalentToRef)
{
    int m = 123;
    int n = 1;
    auto ref_solver = ref_idr_factory->generate(mtx);
    auto omp_solver = omp_idr_factory->generate(d_mtx);
    auto b = gen_mtx(m, n);
    auto x = gen_mtx(m, n);
    auto d_b = Mtx::create(omp);
    auto d_x = Mtx::create(omp);
    d_b->copy_from(b.get());
    d_x->copy_from(x.get());

    ref_solver->apply(b.get(), x.get());
    omp_solver->apply(d_b.get(), d_x.get());

    GKO_ASSERT_MTX_NEAR(d_b, b, 1e-13);
    GKO_ASSERT_MTX_NEAR(d_x, x, 1e-13);
}


TEST_F(Idr, OmpIdrApplyMultipleRHSIsEquivalentToRef)
{
    int m = 123;
    int n = 16;
    auto omp_solver = omp_idr_factory->generate(d_mtx);
    auto ref_solver = ref_idr_factory->generate(mtx);
    auto b = gen_mtx(m, n);
    auto x = gen_mtx(m, n);
    auto d_b = Mtx::create(omp);
    auto d_x = Mtx::create(omp);
    d_b->copy_from(b.get());
    d_x->copy_from(x.get());

    ref_solver->apply(b.get(), x.get());
    omp_solver->apply(d_b.get(), d_x.get());

    GKO_ASSERT_MTX_NEAR(d_b, b, 1e-13);
    GKO_ASSERT_MTX_NEAR(d_x, x, 1e-13);
}


}  // namespace
