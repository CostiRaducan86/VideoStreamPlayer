namespace VilsSharpX.Api
{
    /// <summary>
    /// Service-layer abstraction between the REST command router and the WPF application.
    /// The implementation (in MainWindow) is responsible for marshaling calls onto the UI
    /// thread. REST handlers must depend ONLY on this interface and never touch WPF controls.
    /// </summary>
    public interface IGuiAutomationBridge
    {
        /// <summary>True while a simulation/capture session is active.</summary>
        bool IsRunning { get; }

        /// <summary>True while the active session is paused.</summary>
        bool IsPaused { get; }

        /// <summary>Starts the simulation/capture pipeline at the given frame rate.</summary>
        void StartSimulation(int fps);

        /// <summary>Stops the simulation/capture pipeline.</summary>
        void StopSimulation();

        /// <summary>Pauses the active session.</summary>
        void PauseSimulation();

        /// <summary>Resumes a paused session.</summary>
        void ResumeSimulation();

        /// <summary>
        /// Applies comparison settings. Only non-null fields are applied.
        /// </summary>
        void SetComparisonSettings(int? mode, int? deadband, int? bDelta);

        /// <summary>Returns the most recent comparison statistics.</summary>
        ComparisonStats GetComparisonStats();

        /// <summary>
        /// Returns a PNG-encoded snapshot of the requested pane.
        /// </summary>
        /// <param name="pane">"A", "B", or "D" (case-insensitive).</param>
        byte[] GetFrameSnapshotPng(string pane);
        
        /// <summary>
        /// Returns the current CAN/UART state, including live data and paging info.
        /// </summary>
        CanUartState GetCanUartState();

        /// <summary>Clears the CAN/UART state.</summary>
        void ClearCanUart();

        /// <summary>Starts CAN/UART recording.</summary>
        void StartCanUartRecording();

        /// <summary>Stops CAN/UART recording.</summary>
        void StopCanUartRecording();

        /// <summary>Moves to the previous CAN/UART page.</summary>
        void PreviousCanUartPage();

        /// <summary>Moves to the next CAN/UART page.</summary>
        void NextCanUartPage();

        /// <summary>Sets the current CAN/UART page.</summary>
        /// <param name="page">The page number to set.</param>
        void SetCanUartPage(int page);
    }
}
