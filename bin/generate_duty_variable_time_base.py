
# Represents the mains power frequency
import math
import os
import sys

MAINS_HZ = 60

# How many cycles of the mains our duty cycle must cover at a minimum
# That is, going lower than this is not good for a resistice load
MIN_CYCLES = 3

RMT_CLK_DIV = 160

# Max value a tick can possibly take as per esp-idf see rmt_item32_t
RMT_TICK_MAX = 32767


def _generate_rmt(duty, period, n):
    # This is the period for one RMT pulse
    rmt_pulse = 1 / (80e6 / RMT_CLK_DIV)

    # Period for one sinusoid, in RMT ticks
    one_sinusoid = (1 / MAINS_HZ) / rmt_pulse

    on_pulses = _generate_pulse(n, one_sinusoid)
    off_pulses = _generate_pulse(period - n, one_sinusoid)

    # print(f"n={n} period={period}:")

    # see if we need to spread the last pulse, as they need to be in pairs
    total_pulses = len(on_pulses) + len(off_pulses)
    spread_last_pulse = False
    if total_pulses % 2 is not 0:
        spread_last_pulse = True

    count = 0

    # generate ON Pulses
    print(f"static const struct rmt_item32_s s_rmt_duty_{duty}[] = {{")
    for pulse in on_pulses:
        if count % 2 == 0:
            if count > 0:
                print("}}},")
            sys.stdout.write("    {{{ ")

        if count == (total_pulses - 1) and spread_last_pulse:
            sys.stdout.write(f"{int(pulse/2)}, 1, {int(pulse/2)}, 1, ")
        else:
            sys.stdout.write(f"{int(pulse)}, 1, ")

        count += 1

    # generate Off Pulses
    for pulse in off_pulses:
        if count % 2 == 0:
            if count > 0:
                print("}}},")
            sys.stdout.write("    {{{ ")

        # check if last pulse needs to be spread
        if count == (total_pulses - 1) and spread_last_pulse:
            sys.stdout.write(f"{int(pulse/2)}, 0, {int(pulse/2)}, 0, ")
        else:
            sys.stdout.write(f"{int(pulse)}, 0, ")

        count += 1

    print("}}},")

    # RMT end marker
    print("    {{{ 0, 1, 0, 0 }}}")
    print("};")
    return int(1 + math.ceil(count / 2))


def _generate_pulse(n, one_sinusoid):
    pulses = []
    while n > 0:
        if n >= MIN_CYCLES:
            n -= MIN_CYCLES
            pulses.append(MIN_CYCLES * one_sinusoid)
        else:
            pulses.append(n * one_sinusoid)
            n = 0
    return pulses


def main():
    print("Generating duty cycle time base")
    print()

    print("#include <soc/rmt_struct.h>")
    print("#include \"rmt_duty_map.h\"")
    print()
    max_cycles = MIN_CYCLES
    rmt_pulses_sizes = []
    for duty in range(0, 101):
        n = max_cycles * duty

        # Now reduce
        diviser = duty
        period = max_cycles * 100 if duty is not 0 else 6
        while diviser > 1:
            if period % diviser == 0 and n % diviser == 0:
                if n / diviser >= MIN_CYCLES:
                    period = int(period / diviser)
                    n = int(n / diviser)
            diviser -= 1
        rmt_pulses_sizes.append(_generate_rmt(duty, period, n))

    # And now generate the map
    print(f"const struct rmt_pulse_t rmt_cycle_duty_map_{MAINS_HZ}hz[101] = {{")
    for d in range(101):
        print(f"    {{ s_rmt_duty_{d}, {rmt_pulses_sizes[d]} }},")
    print(f"}};")


if __name__ == "__main__":
    # execute only if run as a script
    main()
