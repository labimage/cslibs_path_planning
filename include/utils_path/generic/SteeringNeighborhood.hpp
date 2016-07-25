#ifndef STEERINGNEIGHBORHOOD_HPP
#define STEERINGNEIGHBORHOOD_HPP

#include <utils_path/generic/NonHolonomicNeighborhood.hpp>
#include <utils_path/generic/SteeringNode.hpp>

namespace lib_path
{

struct SteeringMoves
{
    enum AllowedMoves {
        FORWARD = 3,
        FORWARD_BACKWARD = 6
    };
};

template <int distance, int steer_steps, int steer_step_deg, int max_steer, int la, int moves = SteeringMoves::FORWARD_BACKWARD, bool reversed = false,
          int straight_dir_switch = static_cast<int>(std::round((la / (double) distance)))>
struct SteeringNeighborhood :
        public NonHolonomicNeighborhood<distance, max_steer, moves, reversed, straight_dir_switch> {
    typedef NonHolonomicNeighborhood<distance, max_steer, moves, reversed, straight_dir_switch> Parent;

    static_assert(steer_step_deg * steer_steps <= max_steer, "max steer is too small");

    using Parent::distance_step_pixel;

    using Parent::SIZE;
    using Parent::PR_ADDED_TO_OPEN_LIST;

    using Parent::STEER_ANGLE;
    static constexpr int MAX_STEER_ANGLE = STEER_ANGLE;
    static constexpr double STEER_DELTA = steer_step_deg;
    static constexpr double LA = la / 100.0;

    static constexpr double PENALTY_BACKWARD = 2.5;
    static constexpr double PENALTY_TURN = 4.0;

    using Parent::resolution;
    using Parent::distance_step;

    template <class PointT>
    struct NodeHolder {
        typedef SteeringNode<PointT> NodeType;
    };

    template <class NodeType>
    static bool isGoal(const NodeType* goal, const NodeType* reference) {
        double delta_rot = std::atan2(std::sin(goal->theta - reference->theta),
                                      std::cos(goal->theta - reference->theta));
        static const double angle_threshold = M_PI / 8;
        if(std::abs(delta_rot) > angle_threshold) {
            return false;
        }

        double eff_gx = goal->x;
        double eff_gy = goal->y;

        if(reversed) {
            if(reference->custom > 0) {
                return false;
            }

            // if we begin at the goal and go backwards, we add a special straight segment at the start
            if(!reference->forward) {
                double theta = goal->theta;
                double dx = -std::cos(theta) * LA / resolution;
                double dy = -std::sin(theta) * LA / resolution;

                eff_gx += dx;
                eff_gy += dy;
            }
        }

        int delta = 4 * distance;
        if(std::abs(eff_gx - reference->x) > delta ||
                std::abs(eff_gy - reference->y) > delta) {
            return false;
        }

        double cell_dist = hypot(eff_gx - reference->x, eff_gy - reference->y);
        double dist = cell_dist * Parent::resolution;

        // euclidean distance
        if(dist < 0.05) {
            return true;
        }


        // check, if goal is near the segment through reference an its predecessor
        if(reference->prev) {
            Eigen::Vector2d p (reference->prev->x, reference->prev->y);
            Eigen::Vector2d r (reference->x, reference->y);
            Eigen::Vector2d g (eff_gx, eff_gy);

            p *=  Parent::resolution;
            r *=  Parent::resolution;
            g *=  Parent::resolution;

            // calculate distance of goal to the line segment through current and prev
            //  if the projection of goal falls onto the segment, check if the distance is small enough
            double line_distance;
            double l2 = (p-r).squaredNorm();
            if(l2 <= 0.0001) { // v ~= w
                line_distance = (g - p).norm();
            } else {
                double t = (g - p).dot(r - p) / l2;

                t = std::max(0.0, std::min(1.0, t));

                Eigen::Vector2d project = p + t * (r-p);

                line_distance = (g-project).norm();
            }
            return line_distance < 0.1;
        }

        return false;
    }

    template <class NodeType>
    static double advance(const NodeType* reference, int i, int step, double& x_, double& y_, double& theta_, bool& forward_, int& steering_angle_, char& custom, double map_rotation) {
        bool initial = reference->depth < 1;
        if(initial && (i != 0 && i != 5)) {
            return -1;
        }

        int max_steer_angle_for_turn = 0;

        if(reference->custom > 0) {
            // if custom flag is positive, we need to go straight
            if(std::abs(reference->steering_angle) > max_steer_angle_for_turn) {
                return -1;
            }
        }
        if(!reversed) {
            if(initial) {
                // if we begin at the start and go backwards, we add a special straight segment here
                if (i > 3) {
                    return -1;
                }
                if (i == 3) {
                    if(step != 0) {
                        return -1;
                    }

                    forward_ = false;

                    double ds = -LA;
                    double ds_map = ds / resolution;
                    double dx = ds_map * std::cos(reference->theta);
                    double dy = ds_map * std::sin(reference->theta);

                    steering_angle_ = 0;
                    custom = 0;

                    x_ = reference->x + dx;
                    y_ = reference->y + dy;
                    theta_ = reference->theta;
                    return std::abs(ds_map) * PENALTY_BACKWARD;
                }
            }

        }

        double cost = distance_step_pixel;

        int delta = STEER_DELTA * step;

        int steering_angle;
        switch(i) {
        default:
        case 0: case 3: // keep angle
            steering_angle = reference->steering_angle;
            if(step > 0) {
                // only forward is done for step 0
                return -1;
            }
            break;
        case 1: case 4: // right
            if(step == 0) {
                // only forward is done for step 0
                return -1;
            }
            if(reference->steering_angle <= -MAX_STEER_ANGLE) {
                return -1;
            }
            steering_angle = reference->steering_angle - delta;
            break;
        case 2: case 5: // left
            if(step == 0) {
                // only forward is done for step 0
                return -1;
            }
            if(reference->steering_angle >= MAX_STEER_ANGLE) {
                return -1;
            }
            steering_angle = reference->steering_angle + delta;
            break;
        }

        if(steering_angle > MAX_STEER_ANGLE) {
            steering_angle = MAX_STEER_ANGLE;
        } else if(steering_angle < -MAX_STEER_ANGLE) {
            steering_angle = -MAX_STEER_ANGLE;
        }

        //        cost *= std::abs(steering_angle) / MAX_STEER_ANGLE * 1.1;

        // check driving direction
        forward_ = (i < 3) ;
        double dir = forward_ ? 1.0 : -1.0;
        if(reversed) {
            dir *= -1.0;
        }

        steering_angle_ = steering_angle;

        double r_world = LA / std::tan(steering_angle / 180. * M_PI);
        if(reversed) {
            //            r_world *= -1.0;
        }
        double ds_world = distance_step * dir;
        double ds_map = distance_step_pixel * dir;

        double r_map = r_world / resolution;

        bool direction_switch = reference->forward != forward_;
        if(direction_switch) {
            if(reference->custom > 0) {
                return -1;
            }

            // only allow to drive straight, if direction changes!
            if(std::abs(steering_angle) > max_steer_angle_for_turn) {
                return -1;
            }

            const NodeType* test = reference;
            for(int straight_parts = 0; straight_parts < straight_dir_switch - 1; ++straight_parts) {
                if(!test->prev) {
                    return -1;
                }

                test = dynamic_cast<const NodeType*>(test->prev);
                if(!test) {
                    throw std::logic_error("cannot cast prev");
                    return -1;
                }

                if(std::abs(test->steering_angle) > max_steer_angle_for_turn) {
                    return -1;
                }
            }
            custom = straight_dir_switch - 1;

        } else {
            if(reference->custom > 0) {
                custom = reference->custom - 1;
            } else {
                custom = 0;
            }
        }


        double dx, dy;
        double dtheta;
        if(std::abs(steering_angle) < 1e-10) {
            dtheta = 0;
            dx = ds_map * std::cos(reference->theta);
            dy = ds_map * std::sin(reference->theta);

        } else {
            dtheta = ds_world / r_world;
            double t = reference->theta + dtheta;
            dx = r_map * (std::sin(t) - std::sin(reference->theta));
            dy = r_map * (-std::cos(t) + std::cos(reference->theta));
        }

        x_ = reference->x + dx;
        y_ = reference->y + dy;
        theta_ = MathHelper::AngleClamp(reference->theta + dtheta);

        if(!forward_) {
            // penalize driving backwards
            cost *= PENALTY_BACKWARD;
        }

        // penalize directional changes
        if(direction_switch/* && !forward_*/) {
            cost *= PENALTY_TURN;
        }


        return cost * 1;
    }

    template <class T, class Map, class NodeType>
    static void iterateFreeNeighbors(T& algo, Map& map, NodeType* reference) {
        double map_rotation = map.getMap()->getRotation();

        for(unsigned i = 0; i < SIZE; ++i) {
            for(unsigned step = 0; step < steer_steps; ++step) {
                double to_x,to_y, to_theta;
                bool forward;
                char custom;
                int steering_angle;
                double cost = advance(reference, i, step, to_x,to_y,to_theta,forward,steering_angle,custom, map_rotation);

                if(cost < 0) {
                    continue;
                }

                if(map.contains(to_x, to_y)) {
                    //                bool free = map.isFree(reference->x,reference->y, to_x,to_y);
                    bool free_or_unknown = map.isFreeOrUnknown(reference->x,reference->y, to_x,to_y);
                    bool can_be_used = free_or_unknown;// free || (free_or_unknown && forward);
                    if(can_be_used) {
                        NodeType* n = map.lookup(to_x, to_y, to_theta, steering_angle, forward);

                        if(n == NULL/* || map.isOccupied(n)*/) {
                            continue;
                        }

                        if(algo.processNeighbor(reference, n, cost) == PR_ADDED_TO_OPEN_LIST)  {
                            n->custom  = custom;
                            n->depth = reference->depth + 1;

                            n->steering_angle = steering_angle;

                            n->x = to_x;
                            n->y = to_y;
                            n->theta = to_theta;
                            n->forward = forward;
                        }

//                        // if we already have maximum steering, the following steps can be skipped
//                        if(steering_angle >= MAX_STEER_ANGLE) {
//                            step = steer_steps;
//                        } else if(steering_angle <= -MAX_STEER_ANGLE) {
//                            step = steer_steps;
//                        }

                    }
                }
            }
        }
    }
};

}

#endif // STEERINGNEIGHBORHOOD_HPP
