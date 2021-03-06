#ifndef _vode_dvjac_H_
#define _vode_dvjac_H_

#include <vode_type.H>
#include <vode_linpack.H>
#include <vode_rhs.H>

AMREX_GPU_HOST_DEVICE AMREX_FORCE_INLINE
void dvjac (IArray1D& pivot, int& IERPJ, burn_t& state, dvode_t& vstate)
{

    // dvjac is called by dvnlsd to compute and process the matrix
    // P = I - h*rl1*J , where J is an approximation to the Jacobian
    // that we obtain either through direct evaluation or caching from
    // a previous evaluation. P is then subjected to LU decomposition
    // in preparation for later solution of linear systems with P as
    // coefficient matrix. This is done by DGEFA.

    IERPJ = 0;

#ifndef AMREX_USE_GPU
    // See whether the Jacobian should be evaluated. Start by basing
    // the decision on whether we're caching the Jacobian.

    int evaluate_jacobian = 1;

    if (use_jacobian_caching) {
        evaluate_jacobian = 0;
    }

    if (use_jacobian_caching) {
        // Now evaluate the cases where we're caching the Jacobian but aren't
        // going to be using the cached Jacobian.

        // On the first step we don't have a cached Jacobian. Also, after enough
        // steps, we consider the cached Jacobian too old and will want to re-evaluate
        // it, so we look at whether the step of the last Jacobian evaluation (NSLJ)
        // is more than max_steps_between_jacobian_evals steps in the past.
        if (vstate.NST == 0 || vstate.NST > vstate.NSLJ + max_steps_between_jacobian_evals) {
            evaluate_jacobian = 1;
        }

        // See the non-linear solver for details on these conditions.
        if (vstate.ICF == 1 && vstate.DRC < CCMXJ) {
            evaluate_jacobian = 1;
        }

        if (vstate.ICF == 2) {
            evaluate_jacobian = 1;
        }

    }

    if (evaluate_jacobian == 1) {
#endif

        // We want to evaluate the Jacobian -- now the path depends on
        // whether we're using the numerical or analytic Jacobian.

        if (jacobian == 1) {

            // For the analytic Jacobian, call the user-supplied function.

            // Increment the Jacobian evaluation counter.
            vstate.NJE += 1;

            // Refresh the timestep marker for the last Jacobian evaluation.
            vstate.NSLJ = vstate.NST;

            // Indicate that the Jacobian is current for this solve.
            vstate.JCUR = 1;

            // Initialize the Jacobian to zero
            vstate.jac.zero();

            jac(state, vstate, vstate.jac);

#ifndef AMREX_USE_GPU
            // Store the Jacobian if we're caching.
            if (use_jacobian_caching == 1) {
                vstate.jac_save = vstate.jac;
            }
#endif

        }
        else {

            // For the numerical Jacobian, make N calls to the RHS to approximate it.

            // Increment the Jacobian evaluation counter.
            vstate.NJE += 1;

            // Refresh the timestep marker for the last Jacobian evaluation.
            vstate.NSLJ = vstate.NST;

            // Indicate that the Jacobian is current for this solve.
            vstate.JCUR = 1;

            Real fac = 0.0_rt;
            for (int i = 1; i <= VODE_NEQS; ++i) {
                fac += (vstate.savf(i) * vstate.ewt(i)) * (vstate.savf(i) * vstate.ewt(i));
            }
            fac = std::sqrt(fac / VODE_NEQS);

            Real R0 = 1000.0_rt * std::abs(vstate.H) * UROUND * VODE_NEQS * fac;
            if (R0 == 0.0_rt) {
                R0 = 1.0_rt;
            }

            for (int j = 1; j <= VODE_NEQS; ++j) {
                Real yj = vstate.y(j);

                Real R = amrex::max(std::sqrt(UROUND) * std::abs(yj), R0 / vstate.ewt(j));
                vstate.y(j) += R;
                fac = 1.0_rt / R;

                rhs(vstate.tn, state, vstate, vstate.acor);
                for (int i = 1; i <= VODE_NEQS; ++i) {
                    vstate.jac.set(i, j, (vstate.acor(i) - vstate.savf(i)) * fac);
                }

                vstate.y(j) = yj;
            }

            // Increment the RHS evaluation counter by N.
            vstate.NFE += VODE_NEQS;

#ifndef AMREX_USE_GPU
            // Store the Jacobian if we're caching.
            if (use_jacobian_caching == 1) {
                vstate.jac_save = vstate.jac;
            }
#endif

        }

#ifndef AMREX_USE_GPU
    }
    else {

        // Load the cached Jacobian.

        // Indicate the Jacobian is not current for this step.
        vstate.JCUR = 0;
        vstate.jac = vstate.jac_save;

    }
#endif

    // Multiply Jacobian by a scalar, add the identity matrix
    // (along the diagonal), and do LU decomposition.

    Real hrl1 = vstate.H * vstate.RL1;
    Real con = -hrl1;

    vstate.jac.mul(con);
    vstate.jac.add_identity();

#ifndef NETWORK_SOLVER
    int IER;
    dgefa(vstate.jac, pivot, IER);

    if (IER != 0) {
        IERPJ = 1;
    }
#endif

}

#endif
