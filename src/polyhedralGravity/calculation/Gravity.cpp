#include "Gravity.h"

namespace polyhedralGravity {

    void Gravity::calculate() {
        SPDLOG_INFO("Calculate...");
        auto x = calculateGij();
    }

    CartesianSegmentPropertyVector Gravity::calculateGij() {
        CartesianSegmentPropertyVector g{_polyhedron.countFaces()};
        std::transform(_polyhedron.getFaces().cbegin(), _polyhedron.getFaces().cend(), g.begin(),
                       [&](const auto &face) -> CartesianSegmentProperty {
                           using util::operator-;
                           const auto &node0 = _polyhedron.getNode(face[0]);
                           const auto &node1 = _polyhedron.getNode(face[1]);
                           const auto &node2 = _polyhedron.getNode(face[2]);
                           return {node1 - node0, node2 - node1, node0 - node2};
                       });
        return g;
    }

    CartesianPlanePropertyVector Gravity::calculatePlaneUnitNormals(const CartesianSegmentPropertyVector &g) {
        CartesianPlanePropertyVector planeUnitNormals{g.size()};
        //Calculate N_i as (G_i1 * G_i2) / |G_i1 * G_i2| with * being the cross product
        std::transform(g.cbegin(), g.cend(), planeUnitNormals.begin(), [](const auto &gi) -> Cartesian {
            using namespace util;
            const Cartesian crossProduct = cross(gi[0], gi[1]);
            const double norm = euclideanNorm(crossProduct);
            return crossProduct / norm;
        });
        return planeUnitNormals;
    }

    CartesianSegmentPropertyVector Gravity::calculateSegmentUnitNormals(const CartesianSegmentPropertyVector &g,
                                                                        const CartesianPlanePropertyVector &planeUnitNormals) {
        CartesianSegmentPropertyVector segmentUnitNormals{g.size()};
        //Calculate n_ij as (G_ij * N_i) / |G_ig * N_i| with * being the cross product
        //Outer "loop" over G_i (running i) and N_i calculating n_i
        std::transform(g.cbegin(), g.cend(), planeUnitNormals.cbegin(), segmentUnitNormals.begin(),
                       [](const CartesianSegmentProperty &gi, const Cartesian &Ni) {
                           CartesianSegmentProperty ni{};
                           //Inner "loop" over G_ij (fixed i, running j) with parameter N_i calculating n_ij
                           std::transform(gi.cbegin(), gi.end(), ni.begin(),
                                          [&Ni](const auto &gij) -> Cartesian {
                                              using namespace util;
                                              const Cartesian crossProduct = cross(gij, Ni);
                                              const double norm = euclideanNorm(crossProduct);
                                              return crossProduct / norm;
                                          });
                           return ni;
                       });
        return segmentUnitNormals;
    }

    PlanePropertyVector
    Gravity::calculatePlaneNormalOrientations(const CartesianPlanePropertyVector &planeUnitNormals) {
        PlanePropertyVector planeNormalOrientations(planeUnitNormals.size(), 0.0);
        //Calculate N_i * -G_i1 where * is the dot product and then use the inverted sgn
        std::transform(planeUnitNormals.cbegin(), planeUnitNormals.cend(), _polyhedron.getFaces().begin(),
                       planeNormalOrientations.begin(),
                       [&](const Cartesian &ni, const std::array<size_t, 3> &gi) {
                           using namespace util;
                           //The first vertices' coordinates of the given face consisting of G_i's
                           const auto &Gi1 = _polyhedron.getNode(gi[0]);
                           //We abstain on the double multiplication with -1 in the line above and beyond since two
                           //times multiplying with -1 equals no change
                           return sgn(dot(ni, Gi1));
                       });
        return planeNormalOrientations;
    }


    std::vector<HessianPlane> Gravity::calculateFacesToHessianPlanes(const Cartesian &p) {
        std::vector<HessianPlane> hessianPlanes{_polyhedron.countFaces()};
        //Calculate for each face/ plane/ triangle (here) the Hessian Plane
        std::transform(_polyhedron.getFaces().cbegin(), _polyhedron.getFaces().cend(), hessianPlanes.begin(),
                       [&](const auto &face) -> HessianPlane {
                           using namespace util;
                           const auto &node0 = _polyhedron.getNode(face[0]);
                           const auto &node1 = _polyhedron.getNode(face[1]);
                           const auto &node2 = _polyhedron.getNode(face[2]);

                           return computeHessianPlane(node0, node1, node2, p);
                       });
        return hessianPlanes;
    }

    HessianPlane Gravity::computeHessianPlane(const Cartesian &p, const Cartesian &q,
                                              const Cartesian &r, const Cartesian &origin) {
        using namespace util;
        const auto crossProduct = cross(p - q, p - r);
        const auto res = (origin - p) * crossProduct;
        const double d = res[0] + res[1] + res[2];

        return {crossProduct[0], crossProduct[1], crossProduct[2], d};
    }

    PlanePropertyVector Gravity::calculatePlaneDistances(const std::vector<HessianPlane> &plane) {
        PlanePropertyVector planeDistances(plane.size(), 0.0);
        //For each plane calculate h_p as D/sqrt(A^2 + B^2 + C^2)
        std::transform(plane.cbegin(), plane.cend(), planeDistances.begin(),
                       [](const HessianPlane &plane) -> double {
                           return std::abs(
                                   plane.d / std::sqrt(plane.a * plane.a + plane.b * plane.b + plane.c * plane.c));
                       });
        return planeDistances;
    }

    CartesianPlanePropertyVector
    Gravity::calculateOrthogonalProjectionPointsOnPlane(const std::vector<HessianPlane> &hessianPlanes,
                                                        const CartesianPlanePropertyVector &planeUnitNormals,
                                                        const PlanePropertyVector &planeDistances) {
        CartesianPlanePropertyVector orthogonalProjectionPointsOfP{planeUnitNormals.size()};
        //According to equation (22)
        //Calculate x_P'_i for each plane i
        std::transform(planeUnitNormals.cbegin(), planeUnitNormals.cend(), planeDistances.cbegin(),
                       orthogonalProjectionPointsOfP.begin(), [](const Cartesian &Ni, double hp) {
                    using namespace util;
                    const Cartesian directionCosine = Ni / euclideanNorm(Ni);
                    return abs(directionCosine * hp);
                });
        //Calculate the sign according to flow text after (22)
        CartesianPlanePropertyVector signs{planeUnitNormals.size()};
        std::transform(planeUnitNormals.cbegin(), planeUnitNormals.cend(), hessianPlanes.cbegin(), signs.begin(),
                       [](const Cartesian &Ni, const HessianPlane &plane) {
                           Cartesian sign{};
                           //if -D/A > 0 --> D/A < 0 --> everything is fine, no change
                           //if -D/A < 0 --> D/A > 0 --> change sign if Ni is positive, else no change
                           sign[0] = plane.d / plane.a < 0 ? 1.0 : Ni[0] > 0 ? -1.0 : 1.0;
                           sign[1] = plane.d / plane.b < 0 ? 1.0 : Ni[1] > 0 ? -1.0 : 1.0;
                           sign[2] = plane.d / plane.c < 0 ? 1.0 : Ni[2] > 0 ? -1.0 : 1.0;
                           return sign;
                       });
        //Apply the calculated signs on top of the previously calculated x_P'_i
        std::transform(orthogonalProjectionPointsOfP.cbegin(), orthogonalProjectionPointsOfP.cend(), signs.cbegin(),
                       orthogonalProjectionPointsOfP.begin(), [](const auto &projection, const auto &sign) {
                    using util::operator*;
                    return projection * sign;
                });
        return orthogonalProjectionPointsOfP;
    }

    SegmentPropertyVector
    Gravity::calculateSegmentNormalOrientations(const CartesianSegmentPropertyVector &segmentUnitNormals,
                                                const CartesianPlanePropertyVector &orthogonalProjectionPointsOnPlane) {
        SegmentPropertyVector segmentNormalOrientations{segmentUnitNormals.size()};

        CartesianSegmentPropertyVector x{segmentUnitNormals.size()};

        //First part of equation (23):
        //Calculate x_P' - x_ij^1 (x_P' is the projectionPoint and x_ij^1 is the first vertices of one segment,
        //i.e. the coordinates of the training-planes' nodes
        //The result is saved in x
        std::transform(orthogonalProjectionPointsOnPlane.cbegin(), orthogonalProjectionPointsOnPlane.cend(),
                       _polyhedron.getFaces().cbegin(), x.begin(),
                       [&](const Cartesian &projectionPoint, const std::array<size_t, 3> &face)
                               -> CartesianSegmentProperty {
                           using util::operator-;
                           const auto &node0 = _polyhedron.getNode(face[0]);
                           const auto &node1 = _polyhedron.getNode(face[1]);
                           const auto &node2 = _polyhedron.getNode(face[2]);
                           return {projectionPoint - node0, projectionPoint - node1, projectionPoint - node2};
                       });
        //The second part of equation (23)
        //Calculate n_ij * x_ij with * being the dot product and use the inverted sgn to determine the value of sigma_pq
        //running over n_i and x_i (running i)
        std::transform(segmentUnitNormals.cbegin(), segmentUnitNormals.cend(), x.cbegin(),
                       segmentNormalOrientations.begin(),
                       [](const CartesianSegmentProperty &ni, const CartesianSegmentProperty &xi) {
                           //running over n_ij and x_ij (fixed i, running j)
                           std::array<double, 3> sigmaPQ{};
                           std::transform(ni.cbegin(), ni.cend(), xi.cbegin(), sigmaPQ.begin(),
                                          [](const Cartesian &nij, const Cartesian &xij) {
                                              using namespace util;
                                              return sgn(dot(nij, xij)) * -1.0;
                                          });
                           return sigmaPQ;
                       });
        return segmentNormalOrientations;
    }

    CartesianSegmentPropertyVector Gravity::calculateOrthogonalProjectionPointsOnSegments(
            const CartesianPlanePropertyVector &orthogonalProjectionPointsOnPlane,
            const SegmentPropertyVector &segmentNormalOrientation) {
        CartesianSegmentPropertyVector orthogonalProjectionPointsOfPPrime{orthogonalProjectionPointsOnPlane.size()};

        auto first = thrust::make_zip_iterator(thrust::make_tuple(orthogonalProjectionPointsOnPlane.begin(),
                                                                  segmentNormalOrientation.begin(),
                                                                  _polyhedron.getFaces().begin()));
        auto last = thrust::make_zip_iterator(thrust::make_tuple(orthogonalProjectionPointsOnPlane.end(),
                                                                 segmentNormalOrientation.end(),
                                                                 _polyhedron.getFaces().end()));

        thrust::transform(first, last, orthogonalProjectionPointsOfPPrime.begin(), [&](const auto &tuple) {
            //P' for plane i, sigma_pq[i] with fixed i, the nodes making up plane i
            const auto &pPrime = thrust::get<0>(tuple);
            const auto &sigmaP = thrust::get<1>(tuple);
            const auto &face = thrust::get<2>(tuple);
            auto counterJ = thrust::counting_iterator<unsigned int>(0);

            CartesianSegmentProperty pDoublePrime{};
            thrust::transform(sigmaP.begin(), sigmaP.end(), counterJ, pDoublePrime.begin(),
                              [&](const auto &sigmaPQ, unsigned int j) {
                                  //We actually only accept +0.0 or -0.0, so the equal comparison is ok
                                  if (sigmaPQ == 0.0) {
                                      return pPrime;
                                  } else {
                                      const auto &v1 = _polyhedron.getNode(face[j]);
                                      const auto &v2 = _polyhedron.getNode(face[(j + 1) % 3]);
                                      return calculateOrthogonalProjectionOnSegment(v1, v2, pPrime);
                                  }
                              });
            return pDoublePrime;
        });

        return orthogonalProjectionPointsOfPPrime;
    }

    Cartesian Gravity::calculateOrthogonalProjectionOnSegment(const Cartesian &v1, const Cartesian &v2,
                                                              const Cartesian &pPrime) {
        using namespace util;
        Cartesian pDoublePrime{};
        const Cartesian matrixRow1 = v2 - v1;
        const Cartesian matrixRow2 = cross(v1 - pPrime, matrixRow1);
        const Cartesian matrixRow3 = cross(matrixRow2, matrixRow1);
        const Cartesian d = {dot(matrixRow1, pPrime), dot(matrixRow2, pPrime), dot(matrixRow3, v1)};
        //TODO Could this be more pretty? More efficient?
        Matrix<double, 3, 3> columnMatrix = transpose(Matrix<double, 3, 3>{matrixRow1, matrixRow2, matrixRow3});
        const double determinant = det(columnMatrix);
        pDoublePrime[0] = det(Matrix<double, 3, 3>{d, columnMatrix[1], columnMatrix[2]});
        pDoublePrime[1] = det(Matrix<double, 3, 3>{columnMatrix[0], d, columnMatrix[2]});
        pDoublePrime[2] = det(Matrix<double, 3, 3>{columnMatrix[0], columnMatrix[1], d});
        return pDoublePrime / determinant;
    }

    SegmentPropertyVector
    Gravity::calculateSegmentDistances(const CartesianPlanePropertyVector &orthogonalProjectionPointsOnPlane,
                                       const CartesianSegmentPropertyVector &orthogonalProjectionPointsOnSegment) {
        SegmentPropertyVector segmentDistances{orthogonalProjectionPointsOnPlane.size()};
        std::transform(orthogonalProjectionPointsOnPlane.cbegin(), orthogonalProjectionPointsOnPlane.cend(),
                       orthogonalProjectionPointsOnSegment.cbegin(), segmentDistances.begin(),
                       [](const Cartesian pPrime, const CartesianSegmentProperty &pDoublePrimes) {
                           std::array<double, 3> hp{};
                           std::transform(pDoublePrimes.cbegin(), pDoublePrimes.cend(), hp.begin(),
                                          [&pPrime](const Cartesian &pDoublePrime) {
                                              using namespace util;
                                              return euclideanNorm(pDoublePrime - pPrime);
                                          });
                           return hp;
                       });
        return segmentDistances;
    }


}
