using System.Windows;
using System.Windows.Controls;

namespace VilsSharpX
{
    public partial class AvtpLvdsSimulationControlWindow : Window
    {
        public AvtpLvdsSimulationControlWindow()
        {
            InitializeComponent();
        }

        public ContentControl LvdsContentHost => LvdsHost;
    }
}
