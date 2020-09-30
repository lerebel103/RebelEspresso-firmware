
# Represents the mains power frequency
MAINS_HZ = 50

# How many cycles of the mains our duty cycle must cover at a minimum
# That is, going lower than this is not good for a resistice load
MIN_CYCLES = 3


def main():
    print("Generating duty cycle time base")
    print(f"\t#n, #period")
    print("static uint16_t s_duty_map[][2] = {")
    max_cycles = MIN_CYCLES
    for duty in range(1, 101):
        n = max_cycles * duty

        # Now reduce
        diviser = duty
        period = max_cycles * 100
        while diviser > 1:
            if period % diviser == 0 and n % diviser == 0:
                if n / diviser >= MIN_CYCLES:
                    period = int(period / diviser)
                    n = int(n / diviser)
            diviser -= 1
        print(f"\t{{ {n},\t{period} }}, \t// {int(n / period * 100)}%")
    print("};")


if __name__ == "__main__":
    # execute only if run as a script
    main()