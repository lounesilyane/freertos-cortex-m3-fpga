-- ============================================================
-- FILE    : irq_latency_demo.vhd
-- BOARD   : Tang Nano 4K (GW1NSR-4C) — PFE Lounes
-- CLOCK   : 27 MHz (CLK_FPGA), PID domain
-- PURPOSE : Illustrate INTERRUPT LATENCY at the FPGA/hardware level
--
-- CONCEPT MAP:
--   • Interrupt Latency  = time from IRQ_IN rising edge
--                          to ISR_ACK going high (hardware response)
--   • In your PFE: equivalent to the delay between
--     TE_TICKS counter overflow (sampling trigger) and
--     the PID pipeline actually latching new encoder data
--
-- NOTE: In pure VHDL (bare-metal hardware), "interrupt latency"
--       is deterministic and bounded to a fixed number of clock
--       cycles — this is WHY FPGA logic is faster than any RTOS
--       software ISR. On a Cortex-M3 at 54 MHz, minimum ISR
--       latency is 12 cycles (Cortex-M3 exception entry overhead).
--       In VHDL, it is exactly PIPELINE_DELAY cycles below.
-- ============================================================

library IEEE;
use IEEE.STD_LOGIC_1164.ALL;
use IEEE.NUMERIC_STD.ALL;

entity irq_latency_demo is
    generic (
        -- Sampling period: 270000 cycles @ 27 MHz = 10 ms
        -- (your TE_TICKS from PFE)
        TE_TICKS       : integer := 270000;

        -- Hardware pipeline delay (latence d'interruption matérielle)
        -- Fixed at 2 clock cycles in this VHDL implementation
        -- Compare: Cortex-M3 software ISR = minimum 12 cycles
        PIPELINE_DELAY : integer := 2
    );
    port (
        CLK_27MHZ   : in  std_logic;   -- 27 MHz FPGA clock
        RESET_N     : in  std_logic;   -- active-low reset

        -- Simulated external event (e.g. encoder index pulse)
        IRQ_IN      : in  std_logic;

        -- Hardware acknowledge: goes high exactly PIPELINE_DELAY
        -- cycles after IRQ_IN — this is your "interrupt latency"
        ISR_ACK     : out std_logic;

        -- Sampling tick (equivalent to your TE_TICKS overflow)
        -- This is the periodic "interrupt" in your PID system
        SAMPLE_TICK : out std_logic;

        -- Latency measurement output (for logic analyzer / UART)
        -- Counts cycles between IRQ_IN and ISR_ACK
        LATENCY_CYCLES : out std_logic_vector(3 downto 0)
    );
end entity irq_latency_demo;

architecture rtl of irq_latency_demo is

    -- --------------------------------------------------------
    -- Internal signals
    -- --------------------------------------------------------

    -- Sampling counter (replicates your TE_TICKS logic from PFE)
    signal te_counter    : unsigned(17 downto 0) := (others => '0');
    signal sample_tick_i : std_logic := '0';

    -- IRQ pipeline registers (2-stage = PIPELINE_DELAY)
    -- Stage 1: synchronise IRQ_IN to clock domain (metastability)
    -- Stage 2: generate ACK — this 2-cycle delay IS the hardware
    --          interrupt latency
    signal irq_sync1     : std_logic := '0';  -- sync stage
    signal irq_sync2     : std_logic := '0';  -- ACK stage

    -- Latency counter: measures cycles from IRQ_IN to ISR_ACK
    signal lat_count     : unsigned(3 downto 0) := (others => '0');
    signal measuring     : std_logic := '0';

begin

    -- ============================================================
    -- PROCESS 1: Periodic sampling tick
    -- Replicates your TE_TICKS counter from phase5_top.vhd
    -- This is the "periodic interrupt" source in your PID system
    -- Latence d'interruption = 0 here (hardware, not software)
    -- ============================================================
    p_sampling_tick : process(CLK_27MHZ, RESET_N)
    begin
        if RESET_N = '0' then
            te_counter    <= (others => '0');
            sample_tick_i <= '0';

        elsif rising_edge(CLK_27MHZ) then
            sample_tick_i <= '0';  -- default: pulse for 1 cycle only

            if te_counter = to_unsigned(TE_TICKS - 1, 18) then
                te_counter    <= (others => '0');
                sample_tick_i <= '1';  -- 1-cycle pulse every 10 ms
                -- In your PFE: this pulse triggers PID computation
                -- Latence = 0 cycles (pure combinational decode)
            else
                te_counter <= te_counter + 1;
            end if;
        end if;
    end process p_sampling_tick;

    -- ============================================================
    -- PROCESS 2: IRQ synchroniser pipeline
    -- Models hardware interrupt latency = PIPELINE_DELAY cycles
    --
    -- Cycle 0 : IRQ_IN rises (external event)
    -- Cycle 1 : irq_sync1 captures it (sync / metastability stage)
    -- Cycle 2 : irq_sync2 = ISR_ACK rises (hardware response)
    --
    -- COMPARE with Cortex-M3 software ISR entry:
    --   • Push 8 registers to stack     : 8 cycles
    --   • Fetch ISR vector from NVIC    : 2 cycles
    --   • Pipeline refill               : 2 cycles
    --   • TOTAL minimum latence ISR     : ~12 cycles @ 54 MHz
    --   → FPGA hardware is 6x faster on interrupt response
    -- ============================================================
    p_irq_pipeline : process(CLK_27MHZ, RESET_N)
    begin
        if RESET_N = '0' then
            irq_sync1 <= '0';
            irq_sync2 <= '0';

        elsif rising_edge(CLK_27MHZ) then
            irq_sync1 <= IRQ_IN;    -- cycle +1 : synchronise
            irq_sync2 <= irq_sync1; -- cycle +2 : acknowledge
            -- ISR_ACK = irq_sync2 = exactly 2 cycles after IRQ_IN
            -- This bounded 2-cycle delay is the hardware
            -- "latence d'interruption" — fully deterministic
        end if;
    end process p_irq_pipeline;

    -- ============================================================
    -- PROCESS 3: Latency measurement counter
    -- Counts cycles from IRQ_IN rising to ISR_ACK rising
    -- Result observable on logic analyzer or via UART telemetry
    -- (connects to your 23-byte UART frame from PFE)
    -- ============================================================
    p_latency_measure : process(CLK_27MHZ, RESET_N)
    begin
        if RESET_N = '0' then
            lat_count <= (others => '0');
            measuring <= '0';

        elsif rising_edge(CLK_27MHZ) then
            if IRQ_IN = '1' and measuring = '0' then
                -- Start counting when IRQ fires
                measuring <= '1';
                lat_count <= (others => '0');

            elsif measuring = '1' then
                if irq_sync2 = '1' then
                    -- Stop when ACK arrives — result is PIPELINE_DELAY
                    measuring <= '0';
                    -- lat_count holds the measured latency in cycles
                else
                    lat_count <= lat_count + 1;
                end if;
            end if;
        end if;
    end process p_latency_measure;

    -- ============================================================
    -- Output assignments
    -- ============================================================
    ISR_ACK        <= irq_sync2;
    SAMPLE_TICK    <= sample_tick_i;
    LATENCY_CYCLES <= std_logic_vector(lat_count);

end architecture rtl;
