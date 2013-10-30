#include <algorithm>
#include <cstdlib> // for std::rand()
#include <iostream> // TMP
#include <set>
#include <stdexcept>

#include "math/algo.h"
#include "sfm/poseransac.h"
#include "sfm/pose.h"

SFM_NAMESPACE_BEGIN

PoseRansac2D2D::Options::Options (void)
    : max_iterations(1000)
    , threshold(1e-3)
    , already_normalized(true)
{
}

PoseRansac2D2D::PoseRansac2D2D (Options const& options)
    : opts(options)
{
}

void
PoseRansac2D2D::estimate (Correspondences const& matches, Result* result)
{
    std::vector<int> inliers;
    inliers.reserve(matches.size());
    std::cout << "RANSAC: Running for " << this->opts.max_iterations
        << " iterations..." << std::endl;
    for (int iteration = 0; iteration < this->opts.max_iterations; ++iteration)
    {
        FundamentalMatrix fundamental;
        this->estimate_8_point(matches, &fundamental);
        this->find_inliers(matches, fundamental, &inliers);
        if (inliers.size() > result->inliers.size())
        {
            std::cout << "RANASC: Iteration " << iteration
                << ", inliers " << inliers.size() << " ("
                << (100.0 * inliers.size() / matches.size())
                << "%)" << std::endl;

            result->fundamental = fundamental;
            std::swap(result->inliers, inliers);
            inliers.reserve(matches.size());
        }
    }
}

void
PoseRansac2D2D::estimate_8_point (Correspondences const& matches,
    FundamentalMatrix* fundamental)
{
    if (matches.size() < 8)
        throw std::invalid_argument("At least 8 matches required");

    /*
     * Draw 8 random numbers in the interval [0, matches.size() - 1]
     * without duplicates. This is done by keeping a set with drawn numbers.
     */
    std::set<int> result;
    while (result.size() < 8)
        result.insert(std::rand() % matches.size());

    math::Matrix<double, 3, 8> pset1, pset2;
    std::set<int>::const_iterator iter = result.begin();
    for (int i = 0; i < 8; ++i, ++iter)
    {
        Correspondence const& match = matches[*iter];
        pset1(0, i) = match.p1[0];
        pset1(1, i) = match.p1[1];
        pset1(2, i) = 1.0;
        pset2(0, i) = match.p2[0];
        pset2(1, i) = match.p2[1];
        pset2(2, i) = 1.0;
    }

    /* Compute fundamental matrix using normalized 8-point. */
    math::Matrix<double, 3, 3> T1, T2;
    if (!this->opts.already_normalized)
    {
        sfm::compute_normalization(pset1, &T1);
        sfm::compute_normalization(pset2, &T2);
        pset1 = T1 * pset1;
        pset2 = T2 * pset2;
    }

    sfm::fundamental_8_point(pset1, pset2, fundamental);
    sfm::enforce_fundamental_constraints(fundamental);

    if (!this->opts.already_normalized)
    {
        *fundamental = T2.transposed().mult(*fundamental).mult(T1);
    }
}

void
PoseRansac2D2D::find_inliers (Correspondences const& matches,
    FundamentalMatrix const& fundamental, std::vector<int>* result)
{
    result->resize(0);
    double const squared_thres = this->opts.threshold * this->opts.threshold;
    for (std::size_t i = 0; i < matches.size(); ++i)
    {
        double error = sampson_distance(fundamental, matches[i]);
        if (error < squared_thres)
            result->push_back(i);
    }
}

/* ---------------------------------------------------------------- */

PoseRansac2D3D::Options::Options (void)
    : max_iterations(100)
    , threshold(1e-3)
{
}

PoseRansac2D3D::PoseRansac2D3D (Options const& options)
    : opts(options)
{
}

void
PoseRansac2D3D::estimate (Correspondences2D3D const& corresp,
    Result* result)
{
    std::vector<int> inliers;
    inliers.reserve(corresp.size());
    std::cout << "RANSAC: Running for " << this->opts.max_iterations
        << " iterations..." << std::endl;
    for (int iteration = 0; iteration < this->opts.max_iterations; ++iteration)
    {
        math::Matrix<double, 3, 4> p_matrix;
        this->estimate_6_point(corresp, &p_matrix);
        this->find_inliers(corresp, p_matrix, &inliers);
        if (inliers.size() > result->inliers.size())
        {
            std::cout << "RANASC: Iteration " << iteration
                << ", inliers " << inliers.size() << " ("
                << (100.0 * inliers.size() / corresp.size())
                << "%)" << std::endl;

            result->p_matrix = p_matrix;
            std::swap(result->inliers, inliers);
            inliers.reserve(corresp.size());
        }
    }
}

void
PoseRansac2D3D::estimate_6_point (Correspondences2D3D const& corresp,
    math::Matrix<double, 3, 4>* p_matrix)
{
    if (corresp.size() < 6)
        throw std::invalid_argument("At least 6 correspondences required");

    /* Draw 6 unique random numbers. */
    std::set<int> result;
    while (result.size() < 6)
        result.insert(std::rand() % corresp.size());

    /* Create list of the six selected correspondences. */
    Correspondences2D3D selection;
    selection.reserve(6);
    std::set<int>::const_iterator iter = result.begin();
    for (int i = 0; i < 6; ++i, ++iter)
        selection.push_back(corresp[*iter]);

    /* Obtain pose from the selection. */
    pose_from_2d_3d_correspondences(selection, p_matrix);
}

void
PoseRansac2D3D::find_inliers (Correspondences2D3D const& corresp,
    math::Matrix<double, 3, 4> const& p_matrix,
    std::vector<int>* result)
{
    result->resize(0);
    double const square_threshold = MATH_POW2(this->opts.threshold);
    for (std::size_t i = 0; i < corresp.size(); ++i)
    {
        Correspondence2D3D const& c = corresp[i];
        math::Vec4d p3d(c.p3d[0], c.p3d[1], c.p3d[2], 1.0);
        math::Vec3d p2d = p_matrix * p3d;
        double const square_distance
            = MATH_POW2(p2d[0] / p2d[2] - corresp[i].p2d[0])
            + MATH_POW2(p2d[1] / p2d[2] - corresp[i].p2d[1]);
        if (square_distance < square_threshold)
            result->push_back(i);
    }
}

/* ---------------------------------------------------------------- */

int
compute_ransac_iterations (double inlier_ratio,
    int num_samples,
    double desired_success_rate)
{
    double prob_all_good = math::algo::fastpow(inlier_ratio, num_samples);
    double num_iterations = std::log(1.0 - desired_success_rate)
        / std::log(1.0 - prob_all_good);
    return static_cast<int>(math::algo::round(num_iterations));
}

SFM_NAMESPACE_END
