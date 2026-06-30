using System;
using System.Diagnostics;
using System.Runtime.InteropServices;

namespace BlueOpenServer
{
    public static class KeyboardHook
    {
        private const int WH_KEYBOARD_LL = 13;
        private const int WM_KEYDOWN = 0x0100;
        private const int WM_SYSKEYDOWN = 0x0104;

        private const int VK_TAB = 0x09;
        private const int VK_ESCAPE = 0x1B;
        private const int VK_F4 = 0x73;
        private const int VK_LWIN = 0x5B;
        private const int VK_RWIN = 0x5C;
        private const int VK_CONTROL = 0x11;
        private const int VK_MENU = 0x12; // Alt key
        private const int VK_SPACE = 0x20;

        private delegate IntPtr LowLevelKeyboardProc(int nCode, IntPtr wParam, IntPtr lParam);

        [DllImport("user32.dll", CharSet = CharSet.Auto, SetLastError = true)]
        private static extern IntPtr SetWindowsHookEx(int idHook, LowLevelKeyboardProc lpfn, IntPtr hMod, uint dwThreadId);

        [DllImport("user32.dll", CharSet = CharSet.Auto, SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool UnhookWindowsHookEx(IntPtr hhk);

        [DllImport("user32.dll", CharSet = CharSet.Auto, SetLastError = true)]
        private static extern IntPtr CallNextHookEx(IntPtr hhk, int nCode, IntPtr wParam, IntPtr lParam);

        [DllImport("kernel32.dll", CharSet = CharSet.Auto, SetLastError = true)]
        private static extern IntPtr GetModuleHandle(string lpModuleName);

        [DllImport("user32.dll", CharSet = CharSet.Auto, ExactSpelling = true)]
        private static extern short GetKeyState(int keyCode);

        private static LowLevelKeyboardProc? _proc;
        private static IntPtr _hookId = IntPtr.Zero;

        public static void SetHook()
        {
            if (_hookId == IntPtr.Zero)
            {
                _proc = HookCallback;
                _hookId = SetHook(_proc);
            }
        }

        public static void Unhook()
        {
            if (_hookId != IntPtr.Zero)
            {
                UnhookWindowsHookEx(_hookId);
                _hookId = IntPtr.Zero;
                _proc = null;
            }
        }

        private static IntPtr SetHook(LowLevelKeyboardProc proc)
        {
            using (Process curProcess = Process.GetCurrentProcess())
            using (ProcessModule curModule = curProcess.MainModule!)
            {
                return SetWindowsHookEx(WH_KEYBOARD_LL, proc, GetModuleHandle(curModule.ModuleName), 0);
            }
        }

        private static IntPtr HookCallback(int nCode, IntPtr wParam, IntPtr lParam)
        {
            if (nCode >= 0 && (wParam == (IntPtr)WM_KEYDOWN || wParam == (IntPtr)WM_SYSKEYDOWN))
            {
                int vkCode = Marshal.ReadInt32(lParam);

                bool isAlt = (GetKeyState(VK_MENU) & 0x8000) != 0;
                bool isCtrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;

                // 1. Block Win keys
                if (vkCode == VK_LWIN || vkCode == VK_RWIN)
                {
                    return (IntPtr)1;
                }

                // 2. Block Alt + Tab
                if (vkCode == VK_TAB && isAlt)
                {
                    return (IntPtr)1;
                }

                // 3. Block Alt + F4
                if (vkCode == VK_F4 && isAlt)
                {
                    return (IntPtr)1;
                }

                // 4. Block Alt + Esc / Ctrl + Esc
                if (vkCode == VK_ESCAPE && (isAlt || isCtrl))
                {
                    return (IntPtr)1;
                }

                // 5. Block Alt + Space (Window system menu)
                if (vkCode == VK_SPACE && isAlt)
                {
                    return (IntPtr)1;
                }
            }

            return CallNextHookEx(_hookId, nCode, wParam, lParam);
        }
    }
}
