# Building the Aurix TC397 Firmware (DMA + Dual Buffer)

## Project Configuration

This is an **Aurix Development Studio (Eclipse CDT)** project for the TC397 TriCore microcontroller with TASKING compiler.

**Build System:** Eclipse Managed Builder + Infineon Aurix plugins  
**Toolchain:** TASKING TriCore compiler (comes with Aurix Development Studio)  
**Target Configuration:** TriCore Debug (TASKING) configuration

---

## Build Method 1: Aurix Development Studio GUI (Recommended)

### Prerequisites

- **Aurix Development Studio** (ADS) installed (includes TASKING compiler)
  - Download: <https://www.infineon.com/cms/en/tools/aurix-development-studio/>
- Project already imported in ADS

### Steps

1. **Open Aurix Development Studio**
2. **Import or open the project:**

   ```powershell
   File → Open Projects from File System
   → C:\...\VilsSharpX\VilsSharpX\Aurix_Firmware
   ```

3. **Select Build Configuration:**

   - Right-click project → Build Configurations → Set Active → "TriCore Debug (TASKING)"

4. **Clean the project:**

   - Right-click project → Clean Project

5. **Build the project:**

   - Right-click project → Build Project
   - Or: Project → Build All (Ctrl+B)

6. **Expected Success Output:**

   ```text
   Building: asclin1_dma.c
   Building: can_hw.c
   Building: can_diag.c
   Building: Cpu0_Main.c
   Building: rxmon.c
   Building: osram_frame.c
   ...
   Build Finished
   [RESULT] VilsSharpX.elf
   ```

7. **Output Artifacts:**

   - **Main ELF:** `TriCore Debug (TASKING)/VilsSharpX.elf`
   - **Hex File:** `TriCore Debug (TASKING)/VilsSharpX.hex`
   - **Map:** `TriCore Debug (TASKING)/VilsSharpX.map`

### Build Output Location

```text
Aurix_Firmware/
├── TriCore Debug (TASKING)/
│   ├── VilsSharpX.elf          ← Main firmware image
│   ├── VilsSharpX.hex          ← Hex format (for programmers)
│   ├── asclin1_dma.o           ← LVDS DMA module
│   ├── can_hw.o                ← Diagnostic UART module
│   ├── Cpu0_Main.o
│   ├── rxmon.o
│   └── ...
```

---

## Build Method 2: Command-Line (Eclipse CDT Headless)

If you have Aurix Development Studio installed, you can build from command line:

```powershell
# On Windows (requires Eclipse CDT + TASKING installed)
# Navigate to project root
cd "C:\...\VilsSharpX\VilsSharpX"

# Use Eclipse headless builder (if installed)
"C:\Program Files (x86)\Infineon\AurixDevelopmentStudio_*\eclipse\eclipse.exe" `
  -noSplash `
  -application org.eclipse.cdt.managedbuilder.core.headlessbuild `
  -import "Aurix_Firmware" `
  -projects "VilsSharpX" `
  -build "TriCore Debug (TASKING)"
```

**Note:** This requires a full ADS installation with paths set correctly.

---

## Build Method 3: Manual TASKING Compiler (Advanced)

If you have the TASKING compiler but not the full ADS IDE:

```powershell
cd "Aurix_Firmware\TriCore Debug (TASKING)"

# Using the generated Makefile (if available)
# E.g., in Windows with TASKING GNU Make
"C:\Program Files (x86)\HighTec\gnumake.exe" -f Makefile clean
"C:\Program Files (x86)\HighTec\gnumake.exe" -f Makefile all
```

---

## Troubleshooting Build Errors

### Error: `asclin1_dma.h: No such file or directory`

- **Cause:** Eclipse hasn't indexed the new files yet
- **Fix:** Right-click project → Index → Rebuild

### Error: `IfxDma.h not found`

- **Cause:** iLLD include paths not configured
- **Fix:**
  1. Right-click project → Properties → C/C++ Build → Settings
  2. Check GCC C Compiler → Include Paths
  3. Ensure iLLD path is included (usually `Libraries/iLLD`)

### Error: `undefined reference to 'IfxDma_DmaChannel_init'`

- **Cause:** iLLD DMA library not linked, or not compiled
- **Fix:**
  1. Check `Libraries/` folder for iLLD source files
  2. Ensure iLLD is configured to build DMA module
  3. Re-build iLLD library first if needed

### Error: `VilsSharpX.elf not created`

- **Cause:** Link phase failed
- **Fix:**
  1. Clean project completely: Right-click → Clean Project
  2. Check build console for linker errors
  3. Verify Linker Script: `Lcf_Tasking_Tricore_Tc.lsl` (should be in project root)

---

## Verifying the Build

### Check Object Files Were Created

After successful build, verify key files exist:

```powershell
# Check if LVDS DMA object was compiled
Test-Path "Aurix_Firmware\TriCore Debug (TASKING)\asclin1_dma.o"

# Check if diagnostic UART object was compiled
Test-Path "Aurix_Firmware\TriCore Debug (TASKING)\can_hw.o"

# Check main ELF was linked
Test-Path "Aurix_Firmware\TriCore Debug (TASKING)\VilsSharpX.elf"
```

### Check Map File for Symbols

```bash
grep -i "asclin1_dma\|diag_uart" "TriCore Debug (TASKING)/VilsSharpX.map"
```

---

## Next Steps After Successful Build

### Option A: Debug on Target

1. **Use Eclipse Debugger (Recommended):**

   ```text
   Run → Debug As → Embedded C/C++ Application (TASKING)
   ```

2. **Watch Variables:**

   - LVDS: `g_asclin1_dma.completionCount` (should reach ~48/sec)
   - Diagnostic: `g_diagUartStats.dmaCompletions` (should increment)
   - Frame: `g_osramStats.framesOk` (should increment)

### Option B: Flash to Hardware

1. **Connect TC397 debugger** (J-Link, Segger, etc.)
2. **In Eclipse:** Run → Debug As → (TASKING configured debug target)
3. **Flash automatically and break at `main()`**

---

## File Dependencies

The Aurix build system tracks:

- **asclin1_dma.c** depends on: `asclin1_dma.h`, `IfxDma.h`, `IfxAsclin.h` (from iLLD)
- **can_hw.c** depends on: `can_hw.h`, `IfxDma.h`, `IfxAsclin.h` (from iLLD)
- **Cpu0_Main.c** depends on: `asclin1_dma.h`, `can_hw.h`, `can_diag.h`, etc.

---

## Success Criteria

After build completes successfully, you should have:

✅ **VilsSharpX.elf** created (main firmware)
✅ **Compilation log** shows "0 error, 0 warning"
✅ **asclin1_dma.o** + **can_hw.o** object files created

---

## Reporting Build Issues

If build fails, provide:

1. **Full console output** from Eclipse Build panel
2. **Error message** (copy entire error line)
3. **Verification** that files exist:

   ```powershell
   Test-Path "Aurix_Firmware/asclin1_dma.c"          # Should be True
   Test-Path "Aurix_Firmware/can_hw.c"                # Should be True
   Test-Path "Aurix_Firmware/.project"                # Should be True
   ```

4. **TASKING compiler version:**

   ```text
   In Eclipse: Help → About Aurix Development Studio
   ```
