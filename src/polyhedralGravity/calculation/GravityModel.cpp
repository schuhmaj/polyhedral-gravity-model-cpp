#include "GravityModel.h"

namespace polyhedralGravity {

    GravityModelResult GravityModel::evaluate(
            const Polyhedron &polyhedron, double density, const Array3 &computationPoint) {
        using namespace util;
        auto gijVectors = calculateSegmentVectors(polyhedron);
        auto planeUnitNormals = calculatePlaneUnitNormals(gijVectors);
        auto segmentUnitNormals = calculateSegmentUnitNormals(gijVectors, planeUnitNormals);

        auto planeNormalOrientation = calculatePlaneNormalOrientations(computationPoint, polyhedron, planeUnitNormals);

        auto hessianPlanes = calculateFacesToHessianPlanes(computationPoint ,polyhedron);

        auto planeDistances = calculatePlaneDistances(hessianPlanes);

        auto orthogonalProjectionOnPlane =
                calculateOrthogonalProjectionPointsOnPlane(hessianPlanes, planeUnitNormals, planeDistances);

        auto segmentNormalOrientation =
                calculateSegmentNormalOrientations(computationPoint, polyhedron, segmentUnitNormals,
                                                   orthogonalProjectionOnPlane);

        auto orthogonalProjectionOnSegment =
                calculateOrthogonalProjectionPointsOnSegments(computationPoint, polyhedron, orthogonalProjectionOnPlane,
                                                              segmentNormalOrientation);

        auto segmentDistances = calculateSegmentDistances(orthogonalProjectionOnPlane, orthogonalProjectionOnSegment);

        auto distances = calculateDistances(polyhedron, gijVectors, orthogonalProjectionOnSegment);

        auto transcendentalExpressions =
                calculateTranscendentalExpressions(polyhedron, distances, planeDistances, segmentDistances,
                                                   segmentNormalOrientation, orthogonalProjectionOnPlane);

        auto singularities =
                calculateSingularityTerms(polyhedron, gijVectors, segmentNormalOrientation,
                                          orthogonalProjectionOnPlane,
                                          planeDistances, planeNormalOrientation, planeUnitNormals);

        /*
         * Calculate V
         */

        auto zipIteratorV = util::zipPair(planeNormalOrientation, planeDistances, segmentNormalOrientation,
                                          segmentDistances, transcendentalExpressions, singularities);

        double V = std::accumulate(zipIteratorV.first, zipIteratorV.second, 0.0, [](double acc, const auto &tuple) {
            const double sigma_p = thrust::get<0>(tuple);
            const double h_p = thrust::get<1>(tuple);
            const auto &sigmaPQPerPlane = thrust::get<2>(tuple);
            const auto &segmentDistancePerPlane = thrust::get<3>(tuple);
            const auto &transcendentalExpressionsPerPlane = thrust::get<4>(tuple);
            const std::pair<double, std::array<double, 3>> &singularitiesPerPlane = thrust::get<5>(tuple);

            //Sum 1
            auto zipIteratorSum1 = util::zipPair(sigmaPQPerPlane, segmentDistancePerPlane,
                                                 transcendentalExpressionsPerPlane);
            const double sum1 = std::accumulate(zipIteratorSum1.first, zipIteratorSum1.second,
                                                0.0, [](double acc, const auto &tuple) {
                        const double &sigma_pq = thrust::get<0>(tuple);
                        const double &h_pq = thrust::get<1>(tuple);
                        const TranscendentalExpression &transcendentalExpressions = thrust::get<2>(tuple);
                        return acc + sigma_pq * h_pq * transcendentalExpressions.ln;
                    });

            //Sum 2
            auto zipIteratorSum2 = util::zipPair(sigmaPQPerPlane, transcendentalExpressionsPerPlane);
            const double sum2 = std::accumulate(zipIteratorSum2.first, zipIteratorSum2.second,
                                                0.0, [](double acc, const auto &tuple) {
                        const double &sigma_pq = thrust::get<0>(tuple);
                        const TranscendentalExpression &transcendentalExpressions = thrust::get<1>(tuple);
                        return acc + sigma_pq * transcendentalExpressions.an;
                    });

            //Sum up everything
            return acc + sigma_p * h_p * (sum1 + h_p * sum2 + singularitiesPerPlane.first);
        });

        V = (V * util::GRAVITATIONAL_CONSTANT * density) / 2.0;
        SPDLOG_INFO("V= {}", V);


        /*
         * Calculate Vx, Vy, Vz
         * TODO This code cries because it is a code clone and very unhappy about this circumstance
         */

        auto zipIteratorV2 = util::zipPair(planeNormalOrientation, planeDistances, segmentNormalOrientation,
                                           segmentDistances, transcendentalExpressions, singularities,
                                           planeUnitNormals);

        Array3 V2 = std::accumulate(
                zipIteratorV2.first, zipIteratorV2.second, Array3{0.0, 0.0, 0.0},
                [](const Array3 &acc, const auto &tuple) {
                    using namespace util;
                    const double sigma_p = thrust::get<0>(tuple);
                    const double h_p = thrust::get<1>(tuple);
                    const auto &sigmaPQPerPlane = thrust::get<2>(tuple);
                    const auto &segmentDistancePerPlane = thrust::get<3>(tuple);
                    const auto &transcendentalExpressionsPerPlane = thrust::get<4>(tuple);
                    const std::pair<double, std::array<double, 3>> &singularitiesPerPlane = thrust::get<5>(
                            tuple);
                    const Array3 &Np = thrust::get<6>(tuple);

                    //Sum 1
                    auto zipIteratorSum1 = util::zipPair(sigmaPQPerPlane, segmentDistancePerPlane,
                                                         transcendentalExpressionsPerPlane);
                    const double sum1 = std::accumulate(
                            zipIteratorSum1.first, zipIteratorSum1.second, 0.0,
                            [](double acc, const auto &tuple) {
                                const double &sigma_pq = thrust::get<0>(tuple);
                                const double &h_pq = thrust::get<1>(tuple);
                                const TranscendentalExpression &transcendentalExpressions = thrust::get<2>(tuple);
                                return acc + sigma_pq * h_pq * transcendentalExpressions.ln;
                            });

                    //Sum 2
                    auto zipIteratorSum2 = util::zipPair(sigmaPQPerPlane, transcendentalExpressionsPerPlane);
                    const double sum2 = std::accumulate(zipIteratorSum2.first, zipIteratorSum2.second,
                                                        0.0, [](double acc, const auto &tuple) {
                                const double &sigma_pq = thrust::get<0>(tuple);
                                const TranscendentalExpression &transcendentalExpressions = thrust::get<1>(tuple);
                                return acc + sigma_pq * transcendentalExpressions.an;
                            });

                    //Sum up everything
                    return acc + (Np * (sum1 + h_p * sum2 + singularitiesPerPlane.first));
                });

        V2 = abs(V2 * (util::GRAVITATIONAL_CONSTANT * density));

        SPDLOG_INFO("Vx= {}", V2[0]);
        SPDLOG_INFO("Vy= {}", V2[1]);
        SPDLOG_INFO("Vz= {}", V2[2]);

        /*
         * Calculation of Vxx, Vyy, Vzz, Vxy, Vxz, Vyz
         */

        auto zipIteratorV3 = util::zipPair(planeNormalOrientation, planeDistances, segmentNormalOrientation,
                                           segmentDistances, transcendentalExpressions, singularities,
                                           planeUnitNormals, segmentUnitNormals);

        std::array<double, 6> V3 = std::accumulate(
                zipIteratorV3.first, zipIteratorV3.second, std::array<double, 6>{0.0, 0.0, 0.0, 0.0, 0.0, 0.0},
                [](const std::array<double, 6> &acc, const auto &tuple) {
                    using namespace util;
                    const double sigma_p = thrust::get<0>(tuple);
                    const double h_p = thrust::get<1>(tuple);
                    const auto &sigmaPQPerPlane = thrust::get<2>(tuple);
                    const auto &segmentDistancePerPlane = thrust::get<3>(tuple);
                    const auto &transcendentalExpressionsPerPlane = thrust::get<4>(tuple);
                    const std::pair<double, std::array<double, 3>> &singularitiesPerPlane = thrust::get<5>(
                            tuple);
                    const Array3 &Np = thrust::get<6>(tuple);
                    const Array3Triplet &npqPerPlane = thrust::get<7>(tuple);


                    //Sum 1
                    auto zipIteratorSum1 = util::zipPair(npqPerPlane, transcendentalExpressionsPerPlane);
                    const Array3 sum1 = std::accumulate(
                            zipIteratorSum1.first, zipIteratorSum1.second, Array3{0.0, 0.0, 0.0},
                            [](const Array3 &acc, const auto &tuple) {
                                const Array3 &npq = thrust::get<0>(tuple);
                                const TranscendentalExpression &transcendentalExpressions = thrust::get<1>(tuple);
                                return acc + (npq * transcendentalExpressions.ln);
                            });

                    auto sum2Start = thrust::make_zip_iterator(thrust::make_tuple(
                            sigmaPQPerPlane.begin(),
                            transcendentalExpressionsPerPlane.begin()));

                    auto sum2End = thrust::make_zip_iterator(thrust::make_tuple(
                            sigmaPQPerPlane.end(),
                            transcendentalExpressionsPerPlane.end()));

                    //Sum 2
                    auto zipIteratorSum2 = util::zipPair(sigmaPQPerPlane, transcendentalExpressionsPerPlane);
                    const double sum2 = std::accumulate(zipIteratorSum2.first, zipIteratorSum2.second,
                                                        0.0, [](double acc, const auto &tuple) {
                                const double &sigma_pq = thrust::get<0>(tuple);
                                const TranscendentalExpression &transcendentalExpressions = thrust::get<1>(tuple);
                                return acc + sigma_pq * transcendentalExpressions.an;
                            });

                    //Sum up everything
                    const Array3 subSum = (sum1 + (Np * (sigma_p * sum2))) + singularitiesPerPlane.second;

                    const Array3 first = Np * subSum;
                    const Array3 reorderedNp = {Np[0], Np[0], Np[1]};
                    const Array3 reorderedSubSum = {subSum[1], subSum[2], subSum[2]};
                    const Array3 second = reorderedNp * reorderedSubSum;

                    return acc + concat(first, second);
                });

        V3 = V3 * (util::GRAVITATIONAL_CONSTANT * density);

        SPDLOG_INFO("Vxx= {}", V3[0]);
        SPDLOG_INFO("Vyy= {}", V3[1]);
        SPDLOG_INFO("Vzz= {}", V3[2]);
        SPDLOG_INFO("Vxy= {}", V3[3]);
        SPDLOG_INFO("Vxz= {}", V3[4]);
        SPDLOG_INFO("Vyz= {}", V3[5]);

        return {};
    }

    std::vector<Array3Triplet> GravityModel::calculateSegmentVectors(const Polyhedron &polyhedron) {
        std::vector<Array3Triplet> segmentVectors{polyhedron.countFaces()};
        //Calculate G_ij for every plane given as input the three vertices of every face
        std::transform(polyhedron.getFaces().cbegin(), polyhedron.getFaces().cend(), segmentVectors.begin(),
                       [&polyhedron](const std::array<size_t, 3> &face) -> Array3Triplet {
                           const Array3 &vertex0 = polyhedron.getVertex(face[0]);
                           const Array3 &vertex1 = polyhedron.getVertex(face[1]);
                           const Array3 &vertex2 = polyhedron.getVertex(face[2]);
                           return computeSegmentVectorsForPlane(vertex0, vertex1, vertex2);
                       });
        return segmentVectors;
    }

    std::vector<Array3> GravityModel::calculatePlaneUnitNormals(const std::vector<Array3Triplet> &segmentVectors) {
        std::vector<Array3> planeUnitNormals{segmentVectors.size()};
        //Calculate N_p for every plane given as input: G_i0 and G_i1 of every plane
        std::transform(segmentVectors.cbegin(), segmentVectors.cend(), planeUnitNormals.begin(),
                       [](const Array3Triplet &segmentVectorsForPlane) -> Array3 {
                           return computePlaneUnitNormalForPlane(segmentVectorsForPlane[0], segmentVectorsForPlane[1]);
                       });
        return planeUnitNormals;
    }

    std::vector<Array3Triplet> GravityModel::calculateSegmentUnitNormals(
            const std::vector<Array3Triplet> &segmentVectors,
            const std::vector<Array3> &planeUnitNormals) {
        std::vector<Array3Triplet> segmentUnitNormals{segmentVectors.size()};
        //Loop" over G_i (running i=p) and N_p calculating n_p for every plane
        std::transform(segmentVectors.cbegin(), segmentVectors.cend(), planeUnitNormals.cbegin(),
                       segmentUnitNormals.begin(),
                       [](const Array3Triplet &segmentVectorsForPlane, const Array3 &planeUnitNormal) {
                           return computeSegmentUnitNormalForPlane(segmentVectorsForPlane, planeUnitNormal);
                       });
        return segmentUnitNormals;
    }

    std::vector<double>
    GravityModel::calculatePlaneNormalOrientations(const Array3 &computationPoint, const Polyhedron &polyhedron,
                                                   const std::vector<Array3> &planeUnitNormals) {
        std::vector<double> planeNormalOrientations(planeUnitNormals.size(), 0.0);
        auto transformedPolyhedronIt = transformPolyhedron(polyhedron, computationPoint);
        //Calculate sigma_p for every plane given as input: N_p and vertex0 of every face
        std::transform(planeUnitNormals.cbegin(), planeUnitNormals.cend(), transformedPolyhedronIt.first,
                       planeNormalOrientations.begin(),
                       [](const Array3 &planeUnitNormal, const Array3Triplet &face) {
                           //The first vertices' coordinates of the given face consisting of G_i's
                           return computePlaneNormalOrientationForPlane(planeUnitNormal, face[0]);
                       });
        return planeNormalOrientations;
    }

    std::vector<HessianPlane>
    GravityModel::calculateFacesToHessianPlanes(const Array3 &computationPoint, const Polyhedron &polyhedron) {
        std::vector<HessianPlane> hessianPlanes{polyhedron.countFaces()};
        auto transformedPolyhedronIt = transformPolyhedron(polyhedron, computationPoint);
        //Calculate for each face/ plane/ triangle (here) the Hessian Plane
        std::transform(transformedPolyhedronIt.first, transformedPolyhedronIt.second, hessianPlanes.begin(),
                       [](const Array3Triplet &face) -> HessianPlane {
                           using namespace util;
                           //The three vertices put up the plane, p is the origin of the reference system default 0,0,0
                           return computeHessianPlane(face[0], face[1], face[2]);
                       });
        return hessianPlanes;
    }

    std::vector<double> GravityModel::calculatePlaneDistances(const std::vector<HessianPlane> &plane) {
        std::vector<double> planeDistances(plane.size(), 0.0);
        //For each plane compute h_p
        std::transform(plane.cbegin(), plane.cend(), planeDistances.begin(),
                       [](const HessianPlane &plane) -> double {
                           return computePlaneDistanceForPlane(plane);
                       });
        return planeDistances;
    }

    std::vector<Array3> GravityModel::calculateOrthogonalProjectionPointsOnPlane(
            const std::vector<HessianPlane> &hessianPlanes,
            const std::vector<Array3> &planeUnitNormals,
            const std::vector<double> &planeDistances) {
        std::vector<Array3> orthogonalProjectionPointsOfP{planeUnitNormals.size()};

        //Zip the three required arguments together: Plane normal N_i, Plane Distance h_i and the Hessian Form
        auto zip = util::zipPair(planeUnitNormals, planeDistances, hessianPlanes);

        //Calculates the Projection Point P' for every plane p
        thrust::transform(zip.first, zip.second, orthogonalProjectionPointsOfP.begin(), [](const auto &tuple) {
            using namespace util;
            const Array3 &planeUnitNormal = thrust::get<0>(tuple);
            const double planeDistance = thrust::get<1>(tuple);
            const HessianPlane &hessianPlane = thrust::get<2>(tuple);
            return computeOrthogonalProjectionPointsOnPlaneForPlane(planeUnitNormal, planeDistance, hessianPlane);
        });
        return orthogonalProjectionPointsOfP;
    }

    std::vector<Array3> GravityModel::calculateSegmentNormalOrientations(
            const Array3 &computationPoint,
            const Polyhedron &polyhedron,
            const std::vector<Array3Triplet> &segmentUnitNormals,
            const std::vector<Array3> &orthogonalProjectionPointsOnPlane) {
        std::vector<Array3> segmentNormalOrientations{segmentUnitNormals.size()};

        auto transformedPolyhedronIt = transformPolyhedron(polyhedron, computationPoint);
        auto first = util::zip(transformedPolyhedronIt.first,
                               orthogonalProjectionPointsOnPlane.begin(), segmentUnitNormals.begin());
        auto last = util::zip(transformedPolyhedronIt.second,
                              orthogonalProjectionPointsOnPlane.end(), segmentUnitNormals.end());

        //Calculates the segment normal orientation sigma_pq for every plane p
        thrust::transform(first, last, segmentNormalOrientations.begin(), [](const auto &tuple) {
            const Array3Triplet &face = thrust::get<0>(tuple);
            const Array3 &projectionPointOnPlaneForPlane = thrust::get<1>(tuple);
            const Array3Triplet &segmentUnitNormalsForPlane = thrust::get<2>(tuple);

            return computeSegmentNormalOrientationsForPlane(face, projectionPointOnPlaneForPlane,
                                                            segmentUnitNormalsForPlane);
        });
        return segmentNormalOrientations;
    }

    std::vector<Array3Triplet> GravityModel::calculateOrthogonalProjectionPointsOnSegments(
            const Array3 &computationPoint,
            const Polyhedron &polyhedron,
            const std::vector<Array3> &orthogonalProjectionPointsOnPlane,
            const std::vector<Array3> &segmentNormalOrientation) {
        std::vector<Array3Triplet> orthogonalProjectionPointsOnSegments{orthogonalProjectionPointsOnPlane.size()};

        //Zip the three required arguments together: P' for every plane, sigma_pq for every segment, the faces
        auto transformedPolyhedronIt = transformPolyhedron(polyhedron, computationPoint);
        auto first = util::zip(orthogonalProjectionPointsOnPlane.begin(), segmentNormalOrientation.begin(),
                               transformedPolyhedronIt.first);
        auto last = util::zip(orthogonalProjectionPointsOnPlane.end(), segmentNormalOrientation.end(),
                              transformedPolyhedronIt.second);

        //The outer loop with the running i --> the planes
        thrust::transform(first, last, orthogonalProjectionPointsOnSegments.begin(), [](const auto &tuple) {
            //P' for plane i, sigma_pq[i] with fixed i, the nodes making up plane i
            const Array3 &orthogonalProjectionPointOnPlane = thrust::get<0>(tuple);
            const Array3 &segmentNormalOrientationsForPlane = thrust::get<1>(tuple);
            const Array3Triplet &face = thrust::get<2>(tuple);
            return computeOrthogonalProjectionPointsOnSegmentsPerPlane(
                    orthogonalProjectionPointOnPlane, segmentNormalOrientationsForPlane, face);
        });

        return orthogonalProjectionPointsOnSegments;
    }

    std::vector<Array3> GravityModel::calculateSegmentDistances(
            const std::vector<Array3> &orthogonalProjectionPointsOnPlane,
            const std::vector<Array3Triplet> &orthogonalProjectionPointsOnSegment) {
        std::vector<Array3> segmentDistances{orthogonalProjectionPointsOnPlane.size()};
        //The outer loop with the running i --> iterating over planes (P'_i and P''_i are the parameters of the lambda)
        std::transform(orthogonalProjectionPointsOnPlane.cbegin(), orthogonalProjectionPointsOnPlane.cend(),
                       orthogonalProjectionPointsOnSegment.cbegin(), segmentDistances.begin(),
                       [](const Array3 pPrime, const Array3Triplet &pDoublePrimes) {
                           std::array<double, 3> hp{};
                           //The inner loop with the running j --> iterating over the segments
                           //Using the values P'_i and P''_ij for the calculation of the distance
                           std::transform(pDoublePrimes.cbegin(), pDoublePrimes.cend(), hp.begin(),
                                          [&pPrime](const Array3 &pDoublePrime) {
                                              using namespace util;
                                              return euclideanNorm(pDoublePrime - pPrime);
                                          });
                           return hp;
                       });
        return segmentDistances;
    }

    std::vector<std::array<Distance, 3>> GravityModel::calculateDistances(
            const Polyhedron &polyhedron,
            const std::vector<Array3Triplet> &segmentVectors,
            const std::vector<Array3Triplet> &orthogonalProjectionPointsOnSegment) {
        std::vector<std::array<Distance, 3>> distances{segmentVectors.size()};

        //Zip the three required arguments together: G_ij for every segment, P'' for every segment
        auto zip = util::zipPair(segmentVectors, orthogonalProjectionPointsOnSegment, polyhedron.getFaces());

        thrust::transform(zip.first, zip.second, distances.begin(), [&polyhedron](const auto &tuple) {
            const auto &gi = thrust::get<0>(tuple);
            const auto &pDoublePrimePerPlane = thrust::get<1>(tuple);
            const auto &face = thrust::get<2>(tuple);

            std::array<Distance, 3> distancesArray{};
            auto counterJ = thrust::counting_iterator<unsigned int>(0);
            auto innerZip = util::zipPair(gi, pDoublePrimePerPlane);

            thrust::transform(innerZip.first, innerZip.second, counterJ,
                              distancesArray.begin(), [&](const auto &tuple, unsigned int j) {
                        using namespace util;
                        Distance distance{};
                        const Array3 &gijVector = thrust::get<0>(tuple);
                        const Array3 &pDoublePrime = thrust::get<1>(tuple);

                        //Get the vertices (endpoints) of this segment
                        const auto &v1 = polyhedron.getVertex(face[j]);
                        const auto &v2 = polyhedron.getVertex(face[(j + 1) % 3]);

                        //Calculate the 3D distances between P (0, 0, 0) and the segment endpoints v1 and v2
                        distance.l1 = euclideanNorm(v1);
                        distance.l2 = euclideanNorm(v2);
                        //Calculate the 1D distances between P'' (every segment has its own) and the segment endpoints v1 and v2
                        distance.s1 = euclideanNorm(pDoublePrime - v1);
                        distance.s2 = euclideanNorm(pDoublePrime - v2);

                        //Change the sign depending on certain conditions
                        //1D and 3D distance are small
                        if (std::abs(distance.s1 - distance.l1) < 1e-10 &&
                            std::abs(distance.s2 - distance.l2) < 1e-10) {
                            if (distance.s2 < distance.s1) {
                                distance.s1 *= -1.0;
                                distance.s2 *= -1.0;
                                distance.l1 *= -1.0;
                                distance.l2 *= -1.0;
                                return distance;
                            } else if (distance.s2 == distance.s1) {
                                distance.s1 *= -1.0;
                                distance.l1 *= -1.0;
                                return distance;
                            }
                        } else {
                            //condition: P'' lies on the segment described by G_ij
                            const double norm = euclideanNorm(gijVector);
                            if (distance.s1 < norm && distance.s2 < norm) {
                                distance.s1 *= -1.0;
                                return distance;
                            } else if (distance.s2 < distance.s1) {
                                distance.s1 *= -1.0;
                                distance.s2 *= -1.0;
                                return distance;
                            }
                        }
                        return distance;
                    });
            return distancesArray;
        });
        return distances;
    }

    std::vector<std::array<TranscendentalExpression, 3>> GravityModel::calculateTranscendentalExpressions(
            const Polyhedron &polyhedron,
            const std::vector<std::array<Distance, 3>> &distances,
            const std::vector<double> &planeDistances,
            const std::vector<Array3> &segmentDistances,
            const std::vector<Array3> &segmentNormalOrientation,
            const std::vector<Array3> &orthogonalProjectionPointsOnPlane) {
        //The result of this functions
        std::vector<std::array<TranscendentalExpression, 3>> transcendentalExpressions{distances.size()};

        //Zip iterator consisting of  3D and 1D distances l1/l2 and s1/2 | h_p | h_pq | sigma_pq | P'_p | faces
        auto zip = util::zipPair(distances, planeDistances, segmentDistances, segmentNormalOrientation,
                                 orthogonalProjectionPointsOnPlane, polyhedron.getFaces());

        thrust::transform(zip.first, zip.second, transcendentalExpressions.begin(), [&polyhedron](const auto &tuple) {
            const auto &distancesPerPlane = thrust::get<0>(tuple);
            const double hp = thrust::get<1>(tuple);
            const auto &segmentDistancesPerPlane = thrust::get<2>(tuple);
            const auto &segmentNormalOrientationPerPlane = thrust::get<3>(tuple);
            const Array3 &pPrime = thrust::get<4>(tuple);
            const auto &face = thrust::get<5>(tuple);

            auto counterJ = thrust::counting_iterator<unsigned int>(0);


            //Zip iterator consisting of 3D and 1D distances l1/l2 and s1/2 for this plane | sigma_pq for this plane
            auto innerZip =
                    util::zipPair(distancesPerPlane, segmentDistancesPerPlane, segmentNormalOrientationPerPlane);

            //Result for this plane
            std::array<TranscendentalExpression, 3> transcendentalExpressionsPerPlane{};

            thrust::transform(innerZip.first, innerZip.second, counterJ, transcendentalExpressionsPerPlane.begin(),
                              [&](const auto &tuple, const unsigned int j) {
                                  using namespace util;
                                  const Distance &distance = thrust::get<0>(tuple);
                                  const double hpq = thrust::get<1>(tuple);
                                  const double sigmaPQ = thrust::get<2>(tuple);

                                  //Result for this segment
                                  TranscendentalExpression transcendentalExpressionPerSegment{};

                                  //Vertices (endpoints) of this segment
                                  const Array3 &v1 = polyhedron.getVertex(face[(j + 1) % 3]);
                                  const Array3 &v2 = polyhedron.getVertex(face[j]);

                                  //Compute LN_pq according to (14)
                                  //If either sigmaPQ has no sign AND either of the distances of P' to the two
                                  //segment endpoints is zero OR the 1D and 3D distances are below some threshold
                                  //then LN_pq is zero, too
                                  //TODO (distance.s1 + distance.s2 < 1e-10 && distance.l1 + distance.l2 < 1e-10)?!
                                  if ((sigmaPQ == 0.0 &&
                                       (euclideanNorm(pPrime - v1) == 0.0 || euclideanNorm(pPrime - v2) == 0.0)) ||
                                      (distance.s1 + distance.s2 < 1e-10 && distance.l1 + distance.l2 < 1e-10)) {
                                      transcendentalExpressionPerSegment.ln = 0.0;
                                  } else {
                                      transcendentalExpressionPerSegment.ln =
                                              std::log((distance.s2 + distance.l2) / (distance.s1 + distance.l1));
                                  }

                                  //Compute AN_pq according to (15)
                                  //If h_p or h_pq is zero then AN_pq is zero, too
                                  if (hp == 0 || hpq == 0) {
                                      transcendentalExpressionPerSegment.an = 0.0;
                                  } else {
                                      transcendentalExpressionPerSegment.an =
                                              std::atan((hp * distance.s2) / (hpq * distance.l2)) -
                                              std::atan((hp * distance.s1) / (hpq * distance.l1));
                                  }

                                  return transcendentalExpressionPerSegment;
                              });

            return transcendentalExpressionsPerPlane;

        });

        return transcendentalExpressions;
    }

    std::vector<std::pair<double, Array3>> GravityModel::calculateSingularityTerms(
            const Polyhedron &polyhedron,
            const std::vector<Array3Triplet> &segmentVectors,
            const std::vector<Array3> &segmentNormalOrientation,
            const std::vector<Array3> &orthogonalProjectionPointsOnPlane,
            const std::vector<double> &planeDistances,
            const std::vector<double> &planeNormalOrientation,
            const std::vector<Array3> &planeUnitNormals) {
        //The result
        std::vector<std::pair<double, Array3>> singularities{planeDistances.size()};

        //Zip iterator consisting of G_ij vectors | sigma_pq | faces | P' | h_p | sigma_p | N_i
        auto zip = util::zipPair(segmentVectors, segmentNormalOrientation, polyhedron.getFaces(),
                                 orthogonalProjectionPointsOnPlane, planeDistances, planeNormalOrientation,
                                 planeUnitNormals);

        thrust::transform(zip.first, zip.second, singularities.begin(), [&](const auto &tuple) {
            const auto &gijVectorsPerPlane = thrust::get<0>(tuple);
            const auto segmentNormalOrientationPerPlane = thrust::get<1>(tuple);
            const auto &face = thrust::get<2>(tuple);
            const Array3 &pPrime = thrust::get<3>(tuple);
            const double hp = thrust::get<4>(tuple);
            const double sigmaP = thrust::get<5>(tuple);
            const Array3 &Np = thrust::get<6>(tuple);

            //1. case: If all sigma_pq for a given plane p are 1.0 then P' lies inside the plane S_p
            if (std::all_of(segmentNormalOrientationPerPlane.cbegin(), segmentNormalOrientationPerPlane.cend(),
                            [](const double sigma) { return sigma == 1.0; })) {
                using namespace util;
                return std::make_pair(-1.0 * util::PI2 * hp, Np / euclideanNorm(Np) * -1.0 * util::PI2 * sigmaP);
            }
            //2. case: If sigma_pq == 0 AND norm(P' - v1) < norm(G_ij) && norm(P' - v2) < norm(G_ij) with G_ij
            // as the vector of v1 and v2
            // then P' is located on one line segment G_p of plane p, but not on any of its vertices
            auto counterJ = thrust::counting_iterator<unsigned int>(0);
            auto secondCaseBegin = util::zip(gijVectorsPerPlane.begin(), segmentNormalOrientationPerPlane.begin(),
                                             counterJ);
            auto secondCaseEnd = util::zip(gijVectorsPerPlane.end(), segmentNormalOrientationPerPlane.end(),
                                           counterJ + 3);
            if (std::any_of(secondCaseBegin, secondCaseEnd, [&](const auto &tuple) {
                using namespace util;
                const Array3 &gij = thrust::get<0>(tuple);
                const double sigmaPQ = thrust::get<1>(tuple);
                const unsigned int j = thrust::get<2>(tuple);

                if (sigmaPQ != 0.0) {
                    return false;
                }

                const Array3 &v1 = polyhedron.getVertex(face[(j + 1) % 3]);
                const Array3 &v2 = polyhedron.getVertex(face[j]);
                const double gijNorm = euclideanNorm(gij);
                return euclideanNorm(pPrime - v1) < gijNorm && euclideanNorm(pPrime - v2) < gijNorm;
            })) {
                using namespace util;
                return std::make_pair(-1.0 * util::PI * hp, Np / euclideanNorm(Np) * -1.0 * util::PI * sigmaP);
            }
            //3. case If sigma_pq == 0 AND norm(P' - v1) < 0 || norm(P' - v2) < 0
            // then P' is located at one of G_p's vertices
            auto counterJ3 = thrust::counting_iterator<int>(0);
            auto thirdCaseBegin = util::zip(segmentNormalOrientationPerPlane.begin(), counterJ3);
            auto thirdCaseEnd = util::zip(segmentNormalOrientationPerPlane.end(), counterJ3 + 3);
            double e1;
            double e2;
            unsigned int j;
            if (std::any_of(thirdCaseBegin, thirdCaseEnd, [&](const auto &tuple) {
                using namespace util;
                const double sigmaPQ = thrust::get<0>(tuple);
                j = thrust::get<1>(tuple);

                if (sigmaPQ != 0.0) {
                    return false;
                }

                const Array3 &v1 = polyhedron.getVertex(face[(j + 1) % 3]);
                const Array3 &v2 = polyhedron.getVertex(face[j]);
                e1 = euclideanNorm(pPrime - v1);
                e2 = euclideanNorm(pPrime - v2);
                return e1 == 0.0 || e2 == 0.0;
            })) {
                using namespace util;
                const Array3 &g1 = e1 == 0.0 ? gijVectorsPerPlane[j] : gijVectorsPerPlane[(j - 1 + 3) % 3];
                const Array3 &g2 = e1 == 0.0 ? gijVectorsPerPlane[(j + 1) % 3] : gijVectorsPerPlane[j];
                const double gdot = dot(g1 * -1.0, g2);
                const double theta =
                        gdot == 0.0 ? util::PI_2 : std::acos(gdot / (euclideanNorm(g1) * euclideanNorm(g2)));
                return std::make_pair(-1.0 * theta * hp, Np / euclideanNorm(Np) * -1.0 * theta * sigmaP);
            }

            //4. case Otherwise P' is located outside the plane S_p and then the singularity equals zero
            return std::make_pair(0.0, Array3{0, 0, 0});
        });

        return singularities;
    }

    Array3Triplet GravityModel::computeSegmentVectorsForPlane(
            const Array3 &vertex0, const Array3 &vertex1, const Array3 &vertex2) {
        using util::operator-;
        //Calculate G_ij
        return {vertex1 - vertex0, vertex2 - vertex1, vertex0 - vertex2};
    }

    Array3 GravityModel::computePlaneUnitNormalForPlane(const Array3 &segmentVector1, const Array3 &segmentVector2) {
        using namespace util;
        //Calculate N_i as (G_i1 * G_i2) / |G_i1 * G_i2| with * being the cross product
        const Array3 crossProduct = cross(segmentVector1, segmentVector2);
        const double norm = euclideanNorm(crossProduct);
        return crossProduct / norm;
    }

    Array3Triplet GravityModel::computeSegmentUnitNormalForPlane(
            const Array3Triplet &segmentVectors, const Array3 &planeUnitNormal) {
        Array3Triplet segmentUnitNormal{};
        //Calculate n_ij as (G_ij * N_i) / |G_ig * N_i| with * being the cross product
        std::transform(segmentVectors.cbegin(), segmentVectors.end(), segmentUnitNormal.begin(),
                       [&planeUnitNormal](const Array3 &segmentVector) -> Array3 {
                           using namespace util;
                           const Array3 crossProduct = cross(segmentVector, planeUnitNormal);
                           const double norm = euclideanNorm(crossProduct);
                           return crossProduct / norm;
                       });
        return segmentUnitNormal;
    }

    double GravityModel::computePlaneNormalOrientationForPlane(const Array3 &planeUnitNormal, const Array3 &vertex0) {
        using namespace util;
        //Calculate N_i * -G_i1 where * is the dot product and then use the inverted sgn
        //We abstain on the double multiplication with -1 in the line above and beyond since two
        //times multiplying with -1 equals no change
        return sgn(dot(planeUnitNormal, vertex0), util::EPSILON);
    }

    HessianPlane GravityModel::computeHessianPlane(const Array3 &p, const Array3 &q, const Array3 &r) {
        using namespace util;
        constexpr Array3 origin {0.0, 0.0, 0.0};
        const auto crossProduct = cross(p - q, p - r);
        const auto res = (origin - p) * crossProduct;
        const double d = res[0] + res[1] + res[2];

        return {crossProduct[0], crossProduct[1], crossProduct[2], d};
    }

    double GravityModel::computePlaneDistanceForPlane(const HessianPlane &hessianPlane) {
        //Compute h_p as D/sqrt(A^2 + B^2 + C^2)
        return std::abs(hessianPlane.d / std::sqrt(
                hessianPlane.a * hessianPlane.a +
                hessianPlane.b * hessianPlane.b +
                hessianPlane.c * hessianPlane.c));
    }

    Array3 GravityModel::computeOrthogonalProjectionPointsOnPlaneForPlane(
            const Array3 &planeUnitNormal,
            double planeDistance,
            const HessianPlane &hessianPlane) {
        using namespace util;
        //Calculate the projection point by (22) P'_ = N_i / norm(N_i) * h_i
        // norm(N_i) is always 1 since N_i is a "normed" vector --> we do not need this division
        Array3 orthogonalProjectionPoint = planeUnitNormal * planeDistance;

        //Calculate the sign of the projections points x, y, z coordinates and apply it
        //if -D/A > 0 --> D/A < 0 --> everything is fine, no change
        //if -D/A < 0 --> D/A > 0 --> change sign if Ni is positive, else no change
        Array3 intersections = {hessianPlane.a == 0.0 ? 0.0 : hessianPlane.d / hessianPlane.a,
                                hessianPlane.b == 0.0 ? 0.0 : hessianPlane.d / hessianPlane.b,
                                hessianPlane.c == 0.0 ? 0.0 : hessianPlane.d / hessianPlane.c};

        for (unsigned int index = 0; index < 3; ++index) {
            if (intersections[index] < 0) {
                orthogonalProjectionPoint[index] = std::abs(orthogonalProjectionPoint[index]);
            } else {
                if (planeUnitNormal[index] > 0) {
                    orthogonalProjectionPoint[index] = -1.0 * orthogonalProjectionPoint[index];
                }
                orthogonalProjectionPoint[index] = orthogonalProjectionPoint[index];
            }
        }
        return orthogonalProjectionPoint;
    }

    Array3 GravityModel::computeSegmentNormalOrientationsForPlane(
            const Array3Triplet &vertices,
            const Array3 &projectionPointOnPlane,
            const Array3Triplet &segmentUnitNormalsForPlane) {
        using namespace util;
        std::array<double, 3> segmentNormalOrientations{};
        //Equation (23)
        //Calculate x_P' - x_ij^1 (x_P' is the projectionPoint and x_ij^1 is the first vertices of one segment,
        //i.e. the coordinates of the training-planes' nodes --> projectionPointOnPlane - vertex
        //Calculate n_ij * x_ij with * being the dot product and use the inverted sgn to determine the value of sigma_pq
        //--> sgn((dot(segmentNormalOrientation, projectionPointOnPlane - vertex)), util::EPSILON) * -1.0
        std::transform(segmentUnitNormalsForPlane.cbegin(), segmentUnitNormalsForPlane.cend(),
                       vertices.cbegin(), segmentNormalOrientations.begin(),
                       [&projectionPointOnPlane](const Array3 &segmentNormalOrientation, const Array3 &vertex) {
                    using namespace util;
                    return sgn((dot(segmentNormalOrientation, projectionPointOnPlane - vertex)), util::EPSILON) * -1.0;
                });
        return segmentNormalOrientations;
    }

    Array3Triplet GravityModel::computeOrthogonalProjectionPointsOnSegmentsPerPlane(
            const Array3 &projectionPointOnPlane,
            const Array3 &segmentNormalOrientations,
            const Array3Triplet &face) {
        auto counterJ = thrust::counting_iterator<unsigned int>(0);
        Array3Triplet orthogonalProjectionPointOnSegmentPerPlane{};

        //The inner loop with fixed i, running over the j --> the segments of a plane
        thrust::transform(segmentNormalOrientations.begin(), segmentNormalOrientations.end(),
                          counterJ, orthogonalProjectionPointOnSegmentPerPlane.begin(),
                          [&](const double &segmentNormalOrientation, const unsigned int j) {
                              //We actually only accept +0.0 or -0.0, so the equal comparison is ok
                              if (segmentNormalOrientation == 0.0) {
                                  //Geometrically trivial case, in neither of the half space --> already on segment
                                  return projectionPointOnPlane;
                              } else {
                                  //In one of the half space, evaluate the projection point P'' for the segment
                                  //with the endpoints v1 and v2
                                  const auto &vertex1 = face[j];
                                  const auto &vertex2 = face[(j + 1) % 3];
                                  return computeOrthogonalProjectionOnSegment(vertex1, vertex2, projectionPointOnPlane);
                              }
                          });
        return orthogonalProjectionPointOnSegmentPerPlane;
    }

    Array3 GravityModel::computeOrthogonalProjectionOnSegment(const Array3 &vertex1, const Array3 &vertex2,
                                                              const Array3 &orthogonalProjectionPointOnPlane) {
        using namespace util;
        //Preparing our the planes/ equations in matrix form
        const Array3 matrixRow1 = vertex2 - vertex1;
        const Array3 matrixRow2 = cross(vertex1 - orthogonalProjectionPointOnPlane, matrixRow1);
        const Array3 matrixRow3 = cross(matrixRow2, matrixRow1);
        const Array3 d = {dot(matrixRow1, orthogonalProjectionPointOnPlane),
                          dot(matrixRow2, orthogonalProjectionPointOnPlane),
                          dot(matrixRow3, vertex1)};
        Matrix<double, 3, 3> columnMatrix = transpose(Matrix<double, 3, 3>{matrixRow1, matrixRow2, matrixRow3});
        //Calculation and solving the equations of above
        const double determinant = det(columnMatrix);
        return Array3{
                det(Matrix<double, 3, 3>{d, columnMatrix[1], columnMatrix[2]}),
                det(Matrix<double, 3, 3>{columnMatrix[0], d, columnMatrix[2]}),
                det(Matrix<double, 3, 3>{columnMatrix[0], columnMatrix[1], d})
        } / determinant;
    }


}
