#include "PhysicsInfo.hpp"
#include "PhysicsTunings.h"
#include "PhysicsUpgrades.hpp"
#include "Speed/Indep/Libs/Support/Utility/UCrc.h"
#include "Speed/Indep/Libs/Support/Utility/UMath.h"
#include "Speed/Indep/Libs/Support/Utility/UStandard.h"
#include "Speed/Indep/bWare/Inc/bMath.hpp"
#include "Speed/Indep/Src/Sim/UTil.h"
#include "Speed/Indep/Tools/Inc/ConversionUtil.hpp"

using namespace Attrib::Gen;

// Credits: Brawltendo
float Physics::Info::AerodynamicDownforce(const Attrib::Gen::chassis &chassis, const float speed) {
    return speed * 2 * chassis.AERO_COEFFICIENT() * 1000.0f;
}

// Credits: Brawltendo
float Physics::Info::EngineInertia(const Attrib::Gen::engine &engine, const bool loaded) {
    float scale;
    if (loaded) {
        scale = 1.0f;
    } else {
        scale = 0.35f;
    }
    return scale * (engine.FLYWHEEL_MASS() * 0.025f + 0.25f);
}

// Credits: Brawltendo
Physics::Info::eInductionType Physics::Info::InductionType(const Attrib::Gen::induction &induction) {
    if (induction.HIGH_BOOST() > 0.0f || induction.LOW_BOOST() > 0.0f) {
        // turbochargers don't produce significant boost until above the boost threshold (the lowest engine RPM at which it will spool up)
        // meanwhile superchargers apply boost proportionally to the engine RPM, so this param isn't needed there
        if (induction.SPOOL() > 0.0f) {
            return INDUCTION_TURBO_CHARGER;
        } else {
            return INDUCTION_SUPER_CHARGER;
        }
    } else {
        return INDUCTION_NONE;
    }
}

// Credits: Brawltendo
float Physics::Info::NosBoost(const Attrib::Gen::nos &nos, const Tunings *tunings) {
    float torque_scale = 1.0f;
    float boost = nos.TORQUE_BOOST();
    if (tunings != nullptr) {
        boost += boost * tunings->Value[Physics::Tunings::NOS] * 0.25f;
    }
    return boost + torque_scale;
}

// Credits: Brawltendo
float Physics::Info::NosCapacity(const Attrib::Gen::nos &nos, const Tunings *tunings) {
    float capacity = nos.NOS_CAPACITY();
    if (tunings != nullptr) {
        capacity -= capacity * tunings->Value[Physics::Tunings::NOS] * 0.25f;
    }
    return capacity;
}

// Credits: Brawltendo
float Physics::Info::InductionRPM(const Attrib::Gen::engine &engine, const Attrib::Gen::induction &induction, const Tunings *tunings) {
    float spool = induction.SPOOL();

    // tune the (normalized) RPM at which forced induction kicks in
    if ((tunings != nullptr) && spool > 0.0f) {
        float range;
        float value = tunings->Value[Physics::Tunings::INDUCTION];
        if (value < 0.0f) {
            range = spool * 0.25f;
        } else {
            range = (1.0f - spool) * 0.25f;
        }
        spool += range * value;
    }

    // return the unnormalized RPM
    return spool * (engine.RED_LINE() - engine.IDLE()) + engine.IDLE();
}

// Credits: Brawltendo
float Physics::Info::InductionBoost(const Attrib::Gen::engine &engine, const Attrib::Gen::induction &induction, float rpm, float spool,
                                    const Tunings *tunings, float *psi) {
    if (psi != nullptr) {
        *psi = 0.0f;
    }

    spool = UMath::Clamp(spool, 0.0f, 1.0f);
    float rpm_min = engine.IDLE();
    float rpm_max = engine.RED_LINE();
    float induction_boost = 0.f;
    float spool_rpm = InductionRPM(engine, induction, tunings);
    float high_boost = induction.HIGH_BOOST();
    float low_boost = induction.LOW_BOOST();
    float drag = induction.VACUUM();

    if (high_boost > 0.0f || low_boost > 0.0f) {
        // tuning slider adjusts the induction boost bias
        // -tuning produces more low end boost, while +tuning produces more high end boost
        if (tunings != nullptr) {
            float value = tunings->Value[Physics::Tunings::INDUCTION];
            low_boost -= low_boost * value * 0.25f;
            high_boost += high_boost * value * 0.25f;
        }

        if (rpm >= spool_rpm) {
            float induction_ratio = UMath::Ramp(rpm, spool_rpm, rpm_max);
            induction_boost = induction_ratio * high_boost + (1.0f - induction_ratio) * low_boost;
            if (psi != nullptr) {
                *psi = spool * induction.PSI() * UMath::Ramp(induction_boost, 0.0f, UMath::Max(high_boost, low_boost));
            }
        } else if (drag < 0.0f) {
            // apply vacuum effect when not in boost
            float drag_ratio = UMath::Ramp(rpm, rpm_min, spool_rpm);
            induction_boost = drag_ratio * drag;
            if (psi != nullptr) {
                *psi = drag_ratio * -induction.PSI() * UMath::Ramp(-induction_boost, 0.0f, UMath::Max(high_boost, low_boost));
            }
        }
    }

    return induction_boost * spool;
}

// Credits: Brawltendo
float Physics::Info::Torque(const Attrib::Gen::engine &engine, float rpm) {
    float rpm_min = engine.IDLE();
    float rpm_max = engine.MAX_RPM();
    rpm = UMath::Clamp(rpm, engine.IDLE(), engine.RED_LINE());
    unsigned int numpts = engine.Num_TORQUE();
    if (numpts > 1) {
        float ratio;
        unsigned int index = UTIL_InterprolateIndex(numpts - 1, rpm, rpm_min, rpm_max, ratio);
        float power = engine.TORQUE(index);
        unsigned int secondIndex = UMath::Min(numpts - 1, index + 1);
        return UMath::Lerp(power, engine.TORQUE(secondIndex), ratio);
    }

    return 0.0f;
}

// Credits: Brawltendo
Meters Physics::Info::WheelDiameter(const Attrib::Gen::tires &tires, bool front) {
    int axle = front ? 0 : 1;
    float diameter = INCH2METERS(tires.RIM_SIZE().At(axle));
    return diameter + tires.SECTION_WIDTH().At(axle) * 0.001f * 2.0f * (tires.ASPECT_RATIO().At(axle) * 0.01f);
}

// Credits: Brawltendo
// TODO not matching on GC yet
bool Physics::Info::ShiftPoints(const Attrib::Gen::transmission &transmission, const Attrib::Gen::engine &engine,
                                const Attrib::Gen::induction &induction, float *shift_up, float *shift_down, unsigned int numpts) {
    for (int i = 0; i < numpts; ++i) {
        shift_up[i] = 0.0f;
        shift_down[i] = 0.0f;
    }

    unsigned int num_gear_ratios = transmission.Num_GEAR_RATIO();
    if (numpts < num_gear_ratios)
        return false;

    float redline = engine.RED_LINE();
    int topgear = num_gear_ratios - 1;
    int j;
    for (j = G_FIRST; j < topgear; ++j) {
        float g1 = transmission.GEAR_RATIO(j);
        float g2 = transmission.GEAR_RATIO(j + 1);
        float rpm = (redline + engine.IDLE()) * 0.5f;
        float max = rpm;
        int flag = 0;

        if (rpm < redline) {
            // find the upshift RPM for this gear using predicted engine torque
            while (!flag) {
                // seems like the rpm and spool params are swapped in both instances
                // so either it's a mistake that was copy-pasted or it was a deliberate choice
                float currenttorque = Torque(engine, max) * (InductionBoost(engine, induction, 1.0f, max, nullptr, nullptr) + 1.0f);
                float shiftuptorque;
                if (UMath::Abs(g1) > 0.00001f) {
                    float ratio = g2 / g1;
                    float next_rpm = ratio * max;
                    shiftuptorque = Torque(engine, next_rpm) * (InductionBoost(engine, induction, 1.0f, next_rpm, nullptr, nullptr) + 1.0f) * g2 / g1;
                } else {
                    shiftuptorque = 0.0f;
                }

                // set the upshift RPM to the current max
                if (shiftuptorque > currenttorque)
                    break;

                max += 50.0f;
                // set the upshift RPM to the redline RPM
                flag = static_cast<int>(!(max < redline));
            }
            if (!flag) {
                shift_up[j] = max;
            }
        } else {
            flag = 1;
        }
        if (flag) {
            shift_up[j] = redline - 100.0f;
        }

        // calculate downshift RPM for the next gear
        if (UMath::Abs(g1) > 0.00001f) {
            shift_down[j + 1] = (g2 / g1) * shift_up[j];
        } else {
            shift_down[j + 1] = 0.0f;
        }
    }

    shift_up[topgear] = engine.RED_LINE();
    return true;
}

// Credits: Brawltendo
Mps Physics::Info::Speedometer(const Attrib::Gen::transmission &transmission, const Attrib::Gen::engine &engine, const Attrib::Gen::tires &tires,
                               Rpm rpm, GearID gear, const Tunings *tunings) {
    float speed = 0.0f;
    float gear_ratio = transmission.GEAR_RATIO(gear) * transmission.FINAL_GEAR();
    float power_range = engine.RED_LINE() - engine.IDLE();
    gear_ratio = UMath::Abs(gear_ratio);
    if (gear_ratio > 0.0f && power_range > 0.0f) {
        float wheelrear = WheelDiameter(tires, false) * 0.5f;
        float wheelfront = WheelDiameter(tires, true) * 0.5f;
        float avg_wheel_radius = (wheelrear + wheelfront) * 0.5f;
        float clutch_rpm = (rpm - engine.IDLE()) / gear_ratio / power_range * engine.RED_LINE();
        speed = RPM2RPS(clutch_rpm) * avg_wheel_radius;
    }

    float limiter = MPH2MPS(engine.SPEED_LIMITER(0));
    if (limiter > 0.0f) {
        speed = UMath::Min(speed, limiter);
    }

    return speed;
}

// The rest of this file follows the PC 1.3 build (speed.exe).

// PC: 0x8F8928, the weights (TopSpeed, Handling, Acceleration) of each
// Physics::Upgrades::Type part in the performance ratings.
Physics::Info::Performance Physics::Info::PerformanceWeights[7] = {
    Performance(0.25f, 1.5f, 0.25f), // tires
    Performance(0.0f, 0.5f, 0.0f),   // brakes
    Performance(0.25f, 1.0f, 0.2f),  // chassis
    Performance(1.0f, 0.0f, 0.75f),  // transmission
    Performance(0.5f, 0.0f, 1.0f),   // engine
    Performance(0.25f, 0.0f, 1.25f), // induction
    Performance(0.25f, 0.0f, 1.5f),  // nos
};

// PC: 0x672F50
Physics::Info::eInductionType Physics::Info::InductionType(const Attrib::Gen::pvehicle &pvehicle) {
    Attrib::Gen::induction induction(pvehicle.induction(0), 0, nullptr);
    return InductionType(induction);
}

// PC: 0x66BB80
bool Physics::Info::HasNos(const Attrib::Gen::pvehicle &pvehicle) {
    Attrib::Gen::nos nos(pvehicle.nos(0), 0, nullptr);
    return nos.TORQUE_BOOST() > 0.0f && nos.NOS_CAPACITY() > 0.0f;
}

// PC: 0x66BC00
bool Physics::Info::HasRunflatTires(const Attrib::Gen::pvehicle &pvehicle) {
    return false;
}

// PC: 0x66C3A0
float Physics::Info::MaxTorque(const Attrib::Gen::engine &engine, float &atrpm) {
    unsigned int num_torque = engine.Num_TORQUE();
    if (num_torque == 0) {
        atrpm = 0.0f;
        return 0.0f;
    }
    float torque = 0.0f;
    unsigned int max_pt = 0;
    for (unsigned int i = 0; i < engine.Num_TORQUE(); ++i) {
        float pt_torque = engine.TORQUE(i);
        if (torque < pt_torque) {
            torque = pt_torque;
            max_pt = i;
        }
    }
    atrpm = engine.IDLE();
    if (num_torque > 1) {
        float rpm_ratio = static_cast<float>(max_pt) / static_cast<float>(num_torque - 1);
        atrpm = (engine.MAX_RPM() - engine.IDLE()) * rpm_ratio + engine.IDLE();
    }
    atrpm = UMath::Max(UMath::Min(atrpm, engine.RED_LINE()), engine.IDLE());
    return torque;
}

// PC: 0x66C560
float Physics::Info::Redline(const Attrib::Gen::pvehicle &pvehicle) {
    Attrib::Gen::engine engine(pvehicle.engine(0), 0, nullptr);
    return engine.RED_LINE();
}

// PC: 0x66C180
FtLbs Physics::Info::MaxInductedTorque(const Attrib::Gen::engine &engine, const Attrib::Gen::induction &induction, float &atrpm,
                                       const Tunings *tunings) {
    unsigned int num_torque = engine.Num_TORQUE();
    atrpm = engine.IDLE();
    if (num_torque <= 1) {
        return 0.0f;
    }
    float rpm = engine.IDLE();
    float torque = 0.0f;
    float delta_rpm = (engine.MAX_RPM() - engine.IDLE()) / static_cast<float>(num_torque - 1);
    for (unsigned int i = 0; i < engine.Num_TORQUE(); ++i) {
        float pt_torque = (InductionBoost(engine, induction, rpm, 1.0f, tunings, nullptr) + 1.0f) * engine.TORQUE(i);
        if (torque < pt_torque) {
            torque = pt_torque;
            atrpm = rpm;
        }
        rpm += delta_rpm;
    }
    atrpm = UMath::Max(UMath::Min(atrpm, engine.RED_LINE()), engine.IDLE());
    return torque;
}

// PC: 0x66C2D0
FtLbs Physics::Info::MaxInductedTorque(const Attrib::Gen::pvehicle &pvehicle, Rpm &atrpm, const Tunings *tunings) {
    Attrib::Gen::engine engine(pvehicle.engine(0), 0, nullptr);
    Attrib::Gen::induction induction(pvehicle.induction(0), 0, nullptr);
    return MaxInductedTorque(engine, induction, atrpm, tunings);
}

// PC: 0x66BFA0
float Physics::Info::MaxInductedPower(const Attrib::Gen::pvehicle &pvehicle, const Tunings *tunings) {
    Attrib::Gen::engine engine(pvehicle.engine(0), 0, nullptr);
    Attrib::Gen::induction induction(pvehicle.induction(0), 0, nullptr);
    unsigned int num_torque = engine.Num_TORQUE();
    if (num_torque <= 1) {
        return 0.0f;
    }
    float rpm = engine.IDLE();
    float result = 0.0f;
    float delta_rpm = (engine.MAX_RPM() - engine.IDLE()) / static_cast<float>(num_torque - 1);
    for (unsigned int i = 0; i < engine.Num_TORQUE(); ++i) {
        // ft-lb * rpm / 5252 = hp
        float pt_power = (InductionBoost(engine, induction, rpm, 1.0f, tunings, nullptr) + 1.0f) * engine.TORQUE(i) * rpm * 0.00019040366f;
        if (result < pt_power) {
            result = pt_power;
        }
        rpm += delta_rpm;
    }
    return result;
}

// PC: 0x66C9A0
bool Physics::Info::HasPerformanceRatings(const Attrib::Gen::pvehicle &pvehicle) {
    float base_handling = pvehicle.HandlingRating(0);
    float top_handling = pvehicle.HandlingRating(1);
    return base_handling < top_handling && top_handling > 0.0f;
}

// A simulated standing-start run plus a handling estimate
// total size: 0xC
struct PerfStats {
    float Time0To100;     // offset 0x0, size 0x4 seconds to 100 mph
    float TopSpeed;       // offset 0x4, size 0x4 m/s
    float HandlingRating; // offset 0x8, size 0x4

    PerfStats() : Time0To100(0.0f), TopSpeed(0.0f), HandlingRating(0.0f) {}

    bool Fetch(const Attrib::Gen::pvehicle &pvehicle, bVector2 *graph_data, int *num_data);
};

// total size: 0x2C
struct PerfLevel {
    PerfStats Stats;                     // offset 0x0, size 0xC
    Physics::Info::Performance Stock;    // offset 0xC, size 0xC
    Physics::Info::Performance Upgraded; // offset 0x18, size 0xC
    Attrib::Key Key;                     // offset 0x24, size 0x4
    bool Analyzed;                       // offset 0x28, size 0x1

    PerfLevel(Attrib::Key key) : Key(key), Analyzed(false) {}

    // Rates the stats 0..1 between the field's worst and best.
    void Rate(const PerfStats &bottom, const PerfStats &top);
};

DECLARE_CONTAINER_TYPE(PerformanceMaps);

struct PerformanceMaps : public UTL::Std::list<PerfLevel, _type_PerformanceMaps> {
    // Per stat, the extreme value in direction (-1 = minimum, 1 = maximum).
    void FindLimits(float direction, PerfStats &out) const;
};

// PC: 0x92C4E4, the ratings of every stock car
static PerformanceMaps TheStockCars;
static PerfStats bottom_stats; // PC: 0x92C578
static PerfStats top_stats;    // PC: 0x92619C

// PC: 0x678CE0
bool PerfStats::Fetch(const Attrib::Gen::pvehicle &pvehicle, bVector2 *graph_data, int *num_data) {
    Time0To100 = 0.0f;
    TopSpeed = 0.0f;
    HandlingRating = 0.0f;

    Attrib::Gen::engine engine(pvehicle.engine(0), 0, nullptr);
    Attrib::Gen::induction induction(pvehicle.induction(0), 0, nullptr);
    Attrib::Gen::transmission transmission(pvehicle.transmission(0), 0, nullptr);
    Attrib::Gen::chassis chassis(pvehicle.chassis(0), 0, nullptr);
    Attrib::Gen::tires tires(pvehicle.tires(0), 0, nullptr);
    Attrib::Gen::nos nos(pvehicle.nos(0), 0, nullptr);

    Rpm max_torque_rpm;
    Physics::Info::MaxTorque(engine, max_torque_rpm);
    Meters wheel_radius = Physics::Info::WheelDiameter(tires, false) * 0.5f;
    float final_gear = transmission.FINAL_GEAR();
    float idle = engine.IDLE();
    float redline = engine.RED_LINE();
    float min_w = idle * 0.10471976f; // rpm -> rad/s
    float max_w = redline * 0.10471976f;
    float limiter = engine.SPEED_LIMITER(0) * 0.44704f; // mph -> m/s
    float shift_up[12];
    float shift_down[12];

    if (!Physics::Info::ShiftPoints(transmission, engine, induction, shift_up, shift_down, 12)) {
        return false;
    }
    if (!(wheel_radius > 0.0f) || !(final_gear > 0.0f)) {
        return false;
    }

    float mass = pvehicle.MASS();
    float time = 0.0f;
    float speed = 0.0f;
    int data_index = 0;
    // fine steps until 100 mph (unless graphing), then coarse ones to find the top speed
    float dT = graph_data != nullptr ? 1.0f : 0.125f;
    int max_data_index = num_data != nullptr ? *num_data : 0;
    unsigned int num_gears = transmission.Num_GEAR_RATIO();
    unsigned int topgear = (num_gears > 2 ? num_gears - 2 : 0) - 1;
    float ratio = (max_w - min_w) / max_w;
    float inv_radius = 1.0f / wheel_radius;
    float inv_mass = 1.0f / mass;
    unsigned int gear = 0;
    float rpm;
    float acc;

    do {
        float total_gear_ratio = final_gear * transmission.GEAR_RATIO(gear + G_FIRST);
        rpm = (inv_radius * speed * total_gear_ratio * ratio + min_w) * 9.549296f; // rad/s -> rpm
        if (!(rpm < redline)) {
            rpm = redline;
        }
        if (idle > rpm) {
            rpm = idle;
        }
        if (gear == 0 && !(rpm > max_torque_rpm)) {
            rpm = max_torque_rpm;
        }

        float torque = Physics::Info::Torque(engine, rpm);
        float drive_torque =
            (Physics::Info::InductionBoost(engine, induction, rpm, 1.0f, nullptr, nullptr) + 1.0f) * (torque * 1.3558179f) * total_gear_ratio;
        if (time < nos.NOS_CAPACITY() && speed > 3.0f) {
            drive_torque *= nos.TORQUE_BOOST() + 1.0f;
        }
        float force = drive_torque * inv_radius;
        acc = inv_mass * force;
        if (graph_data != nullptr) {
            int index = max_data_index < data_index ? max_data_index : data_index;
            data_index++;
            graph_data[index].x = speed;
            graph_data[index].y = acc;
        }

        float drag = chassis.DRAG_COEFFICIENT() * speed * speed;
        if (drag < 0.0f) {
            drag = -drag;
        }
        float dec = drag * inv_mass;
        speed = acc * dT + speed - dec * dT;
        if (!(speed < 44.7037f) && Time0To100 <= 0.0f) {
            Time0To100 = time;
            dT = 1.0f;
        }

        if (limiter > 0.0f && !(speed < limiter)) {
            TopSpeed = limiter;
        } else if (gear == topgear) {
            if ((dec > acc || !(rpm < redline)) && TopSpeed <= 0.0f) {
                TopSpeed = speed;
            }
        }

        time += dT;
        if (TopSpeed > 0.0f && Time0To100 > 0.0f) {
            break;
        }
        if (!(rpm < shift_up[gear + G_FIRST])) {
            gear++;
            if (gear >= topgear) {
                gear = topgear;
            }
        }
    } while (time < 120.0f);

    if (gear == topgear || TopSpeed <= 0.0f) {
        TopSpeed = speed;
    }

    float base_handling = pvehicle.HandlingRating(0);
    float top_handling = pvehicle.HandlingRating(1);
    float weights = 0.0f;
    float value = 0.0f;
    for (int i = 0; i < Physics::Upgrades::PUT_MAX; ++i) {
        weights += Physics::Info::PerformanceWeights[i].Handling;
        value += Physics::Upgrades::GetPercent(pvehicle, static_cast<Physics::Upgrades::Type>(i)) * Physics::Info::PerformanceWeights[i].Handling;
    }
    if (weights > 1e-6f) {
        value /= weights;
    }
    HandlingRating = (top_handling - base_handling) * value + base_handling;

    if (num_data != nullptr) {
        *num_data = data_index;
    }
    return TopSpeed > 0.0f && Time0To100 > 0.0f;
}

// PC: 0x6794E0
void PerformanceMaps::FindLimits(float direction, PerfStats &out) const {
    out = PerfStats();
    for (const_iterator iter = begin(); iter != end(); ++iter) {
        const PerfStats &stats = iter->Stats;
        if (iter == begin()) {
            out = stats;
            continue;
        }
        if (direction * out.HandlingRating < direction * stats.HandlingRating) {
            out.HandlingRating = stats.HandlingRating;
        }
        if (direction * out.Time0To100 < direction * stats.Time0To100) {
            out.Time0To100 = stats.Time0To100;
        }
        if (direction * out.TopSpeed < direction * stats.TopSpeed) {
            out.TopSpeed = stats.TopSpeed;
        }
    }
}

static float RatePerfStat(float value, float bottom, float top) {
    float range = top - bottom;
    if (!(range > 1e-6f)) {
        return 0.0f;
    }
    float ratio = (value - bottom) / range;
    if (!(ratio < 1.0f)) {
        return 1.0f;
    }
    if (0.0f > ratio) {
        return 0.0f;
    }
    return ratio;
}

// PC: inlined in Physics::Info::Init
void PerfLevel::Rate(const PerfStats &bottom, const PerfStats &top) {
    Stock.Handling = RatePerfStat(Stats.HandlingRating, bottom.HandlingRating, top.HandlingRating);
    Stock.Acceleration = 1.0f - RatePerfStat(Stats.Time0To100, bottom.Time0To100, top.Time0To100);
    Stock.TopSpeed = RatePerfStat(Stats.TopSpeed, bottom.TopSpeed, top.TopSpeed);
}

static bool PerformanceInitialized; // PC: 0x926184

// Rates every stock car, stock and fully upgraded, against the whole field
// (PC: 0x683EB0).
void Physics::Info::Init() {
    const Attrib::Class *aclass = Attrib::Database::Get().GetClass(Attrib::Gen::pvehicle::ClassKey());
    PerformanceMaps all_cars;
    PerformanceMaps upgraded_cars;

    for (Attrib::Key key = aclass->GetFirstCollection(); key != 0; key = aclass->GetNextCollection(key)) {
        Attrib::Gen::pvehicle pvehicle(key, 0, nullptr);
        if (pvehicle.MODEL().GetHash32() == UCrc32::kNull.GetValue() || pvehicle.IsDynamic() || !HasPerformanceRatings(pvehicle)) {
            continue;
        }
        PerfLevel performance(key);
        if (performance.Stats.Fetch(pvehicle, nullptr, nullptr)) {
            TheStockCars.push_back(performance);
            all_cars.push_back(performance);
        }
        Attrib::Database::Get().CollectGarbage();
    }

    for (PerformanceMaps::iterator iter = TheStockCars.begin(); iter != TheStockCars.end(); ++iter) {
        Attrib::Gen::pvehicle pvehicle(iter->Key, 0, nullptr);
        if (!Physics::Upgrades::SetMaximum(pvehicle)) {
            continue;
        }
        PerfLevel performance(iter->Key);
        if (performance.Stats.Fetch(pvehicle, nullptr, nullptr)) {
            upgraded_cars.push_back(performance);
            all_cars.push_back(performance);
        }
        Attrib::Database::Get().CollectGarbage();
    }

    if (TheStockCars.empty()) {
        return;
    }

    all_cars.FindLimits(-1.0f, bottom_stats);
    all_cars.FindLimits(1.0f, top_stats);
    for (PerformanceMaps::iterator iter = TheStockCars.begin(); iter != TheStockCars.end(); ++iter) {
        iter->Rate(bottom_stats, top_stats);
    }
    for (PerformanceMaps::iterator iter = upgraded_cars.begin(); iter != upgraded_cars.end(); ++iter) {
        iter->Rate(bottom_stats, top_stats);
    }
    for (PerformanceMaps::iterator iter = TheStockCars.begin(); iter != TheStockCars.end(); ++iter) {
        iter->Upgraded = iter->Stock;
        for (PerformanceMaps::iterator iter2 = upgraded_cars.begin(); iter2 != upgraded_cars.end(); ++iter2) {
            if (iter->Key == iter2->Key) {
                iter->Upgraded = iter2->Stock;
                break;
            }
        }
    }
    PerformanceInitialized = true;
}

static const PerfLevel *FindStockPerformance(const Attrib::Gen::pvehicle &pvehicle) {
    if (!Physics::Info::HasPerformanceRatings(pvehicle)) {
        return nullptr;
    }
    Attrib::Key key = pvehicle.GetCollection();
    if (pvehicle.IsDynamic()) {
        key = pvehicle.GetParent();
    }
    for (PerformanceMaps::const_iterator iter = TheStockCars.begin(); iter != TheStockCars.end(); ++iter) {
        if (iter->Key == key) {
            return &*iter;
        }
    }
    return nullptr;
}

// PC: 0x679660
bool Physics::Info::GetStockPerformance(const Attrib::Gen::pvehicle &pvehicle, Performance &perf) {
    const PerfLevel *level = FindStockPerformance(pvehicle);
    if (level == nullptr) {
        return false;
    }
    perf = level->Stock;
    return true;
}

// PC: 0x6796C0
bool Physics::Info::GetMaximumPerformance(const Attrib::Gen::pvehicle &pvehicle, Performance &perf) {
    const PerfLevel *level = FindStockPerformance(pvehicle);
    if (level == nullptr) {
        return false;
    }
    perf = level->Upgraded;
    return true;
}

// The car's ratings with its installed parts: each part moves the ratings from
// stock toward fully upgraded by its weight, and junkman parts lift both ends
// (PC: 0x67C570).
bool Physics::Info::EstimatePerformance(const Attrib::Gen::pvehicle &pvehicle, Performance &perf) {
    Performance stock;
    Performance upgraded;
    if (!GetStockPerformance(pvehicle, stock) || !GetMaximumPerformance(pvehicle, upgraded)) {
        return false;
    }
    perf.Default();
    Performance weights;
    Performance junk;
    Performance junk_weights;
    for (int i = 0; i < Physics::Upgrades::PUT_MAX; ++i) {
        const Performance &w = PerformanceWeights[i];
        Physics::Upgrades::Type type = static_cast<Physics::Upgrades::Type>(i);
        float value = Physics::Upgrades::GetPercent(pvehicle, type);
        junk_weights.TopSpeed += w.TopSpeed;
        junk_weights.Handling += w.Handling;
        junk_weights.Acceleration += w.Acceleration;
        if (Physics::Upgrades::GetJunkman(pvehicle, type)) {
            junk.TopSpeed += w.TopSpeed;
            junk.Handling += w.Handling;
            junk.Acceleration += w.Acceleration;
        }
        weights.TopSpeed += w.TopSpeed;
        weights.Handling += w.Handling;
        weights.Acceleration += w.Acceleration;
        perf.TopSpeed += value * w.TopSpeed;
        perf.Handling += value * w.Handling;
        perf.Acceleration += value * w.Acceleration;
    }
    if (weights.Handling > 1e-6f) {
        perf.Handling /= weights.Handling;
    }
    if (weights.Acceleration > 1e-6f) {
        perf.Acceleration /= weights.Acceleration;
    }
    if (weights.TopSpeed > 1e-6f) {
        perf.TopSpeed /= weights.TopSpeed;
    }
    if (junk_weights.Handling > 1e-6f) {
        float j = junk.Handling / junk_weights.Handling;
        upgraded.Handling = (1.0f - upgraded.Handling) * j + upgraded.Handling;
        stock.Handling = (upgraded.Handling - stock.Handling) * j * 0.33f + stock.Handling;
        perf.Handling = UMath::Min((j * 0.33f + 1.0f) * perf.Handling, 1.0f);
    }
    if (junk_weights.Acceleration > 1e-6f) {
        float j = junk.Acceleration / junk_weights.Acceleration;
        upgraded.Acceleration = (1.0f - upgraded.Acceleration) * j + upgraded.Acceleration;
        stock.Acceleration = (upgraded.Acceleration - stock.Acceleration) * j * 0.33f + stock.Acceleration;
        perf.Acceleration = UMath::Min((j * 0.33f + 1.0f) * perf.Acceleration, 1.0f);
    }
    if (junk_weights.TopSpeed > 1e-6f) {
        float j = junk.TopSpeed / junk_weights.TopSpeed;
        upgraded.TopSpeed = (1.0f - upgraded.TopSpeed) * j + upgraded.TopSpeed;
        stock.TopSpeed = (upgraded.TopSpeed - stock.TopSpeed) * j * 0.33f + stock.TopSpeed;
        perf.TopSpeed = UMath::Min((j * 0.33f + 1.0f) * perf.TopSpeed, 1.0f);
    }
    perf.Handling = (upgraded.Handling - stock.Handling) * perf.Handling + stock.Handling;
    perf.Acceleration = (upgraded.Acceleration - stock.Acceleration) * perf.Acceleration + stock.Acceleration;
    perf.TopSpeed = (upgraded.TopSpeed - stock.TopSpeed) * perf.TopSpeed + stock.TopSpeed;
    return true;
}

#if defined(EA_PORT_NATIVE)
#include "Speed/Indep/Src/Misc/Table.hpp"

// PC: 0x673120
FtLbs Physics::Info::AvgInductedTorque(const Attrib::Gen::engine &engine, const Attrib::Gen::induction &induction,
                                       const Attrib::Gen::transmission &transmission, bool from_peak, const Tunings *tunings) {
    if (engine.Num_TORQUE() <= 1) {
        return 0.0f;
    }
    float peak_rpm;
    if (!(MaxInductedTorque(engine, induction, peak_rpm, tunings) > 0.0f)) {
        return 0.0f;
    }
    float converter = transmission.TORQUE_CONVERTER();
    float rpm = engine.IDLE();
    float delta_rpm = (engine.MAX_RPM() - engine.IDLE()) / static_cast<float>(engine.Num_TORQUE() - 1);
    float total = 0.0f;
    float count = 0.0f;
    for (unsigned int i = 0; i < engine.Num_TORQUE(); ++i) {
        if (!from_peak || peak_rpm <= rpm) {
            // the torque converter multiplies torque below the peak
            float multiplier = (1.0f - UMath::Ramp(rpm, engine.IDLE(), peak_rpm)) * converter + 1.0f;
            total += (InductionBoost(engine, induction, rpm, 1.0f, tunings, nullptr) + 1.0f) * engine.TORQUE(i) * multiplier;
            count += 1.0f;
        }
        rpm += delta_rpm;
        if (engine.RED_LINE() <= rpm) {
            break;
        }
    }
    if (count > 0.0f) {
        return total / count;
    }
    return 0.0f;
}

// PC: 0x673810
// Acceleration (m/s^2) against speed, from the redline speed of each forward
// gear, sampled evenly up to the top speed.
bool Physics::Info::ComputeAccelerationTable(const Attrib::Gen::pvehicle &pvehicle, float &top_speed, float *table, int num_entries) {
    Attrib::Gen::transmission transmission(pvehicle.transmission(0), 0, nullptr);
    Attrib::Gen::tires tires(pvehicle.tires(0), 0, nullptr);
    Attrib::Gen::chassis chassis(pvehicle.chassis(0), 0, nullptr);
    Attrib::Gen::engine engine(pvehicle.engine(0), 0, nullptr);
    Attrib::Gen::induction induction(pvehicle.induction(0), 0, nullptr);

    float torque = AvgInductedTorque(engine, induction, transmission, true, nullptr) * 1.3558f; // ft-lb -> N-m
    if (!(torque > 0.0f) || num_entries < 2 || table == nullptr) {
        return false;
    }
    bVector2 points[10];
    unsigned int num_gears = transmission.Num_GEAR_RATIO();
    if (num_gears <= 2) {
        return false;
    }
    float final_gear = transmission.FINAL_GEAR();
    float mass = pvehicle.MASS();
    float wheel_radius = (tires.RIM_SIZE().At(1) * 0.0254f + tires.SECTION_WIDTH().At(1) * 0.01f * tires.ASPECT_RATIO().At(1) * 0.001f) * 0.5f;
    if (!(wheel_radius > 1e-6f)) {
        return false;
    }
    unsigned int num_points = 0;
    top_speed = 0.0f;
    float last_accel = 0.0f;
    float last_speed = 0.0f;
    float inv_radius = 1.0f / wheel_radius;
    // gears 0 and 1 are reverse and neutral
    for (unsigned int gear = 2; num_points < num_gears - 2; ++gear) {
        float ratio = final_gear * transmission.GEAR_RATIO(gear);
        float force = inv_radius * ratio * transmission.GEAR_EFFICIENCY(gear) * torque;
        if (ratio <= 0.0f) {
            return false;
        }
        float speed = (engine.RED_LINE() / ratio) * wheel_radius * 0.10471976f; // rpm -> rad/s
        float accel = (force - speed * chassis.DRAG_COEFFICIENT() * speed) / mass;
        if (accel <= 0.0f) {
            if (last_accel <= 0.0f) {
                break;
            }
            // where this gear's acceleration runs out
            speed = (speed - last_speed) * (1.0f - last_accel / (last_accel - accel)) + last_speed;
            accel = 0.0f;
        }
        last_accel = accel;
        last_speed = speed;
        top_speed = UMath::Max(top_speed, last_speed);
        points[num_points].x = last_speed;
        points[num_points].y = last_accel;
        num_points++;
    }
    if (num_points == 0) {
        return false;
    }
    Graph graph(points, num_points);
    if (!(top_speed > 0.0f)) {
        return false;
    }
    float max_speed = top_speed;
    for (int i = 0; i < num_entries; ++i) {
        table[i] = graph.GetValue(static_cast<float>(i) * (max_speed / static_cast<float>(num_entries - 1)));
    }
    return true;
}
#endif
