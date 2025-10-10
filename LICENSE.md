# MIT License

## UdpLogger - Educational Kernel-Mode Keyboard Logging System

**Copyright (c) 2025 Marek Wesołowski (WESMAR)**

---

## License Grant

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

## Attribution Requirement

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

## Disclaimer of Warranty

**THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.**

---

## About This License

### Why MIT License?

The MIT License is one of the most permissive and widely-adopted open source licenses, chosen for UdpLogger to:

- **Maximize adoption** in security research and education
- **Encourage contribution** from the global security community  
- **Enable academic use** without licensing barriers
- **Maintain simplicity** with minimal legal complexity
- **Ensure compatibility** with other open source projects

### What This Means for Users

✅ **Commercial Use Permitted** - Use UdpLogger in authorized business environments without fees  
✅ **Modification Allowed** - Adapt the code for your specific research needs  
✅ **Distribution Allowed** - Share UdpLogger with others, modified or unmodified  
✅ **Private Use Permitted** - Use internally for research without disclosure requirements  
✅ **Patent Use** - Implicit patent grant from contributors  
✅ **Sublicensing Allowed** - Include UdpLogger in larger projects with different licenses  

### What This Requires

📋 **Include License** - This license text must be included with any distribution  
📋 **Include Copyright** - The copyright notice must remain intact  
📋 **No Trademark Rights** - Does not grant rights to use author's trademarks  

---

## Project Information

### Repository Details
- **Project Name:** UdpLogger - Educational Kernel-Mode Keyboard Logging System
- **Version:** 1.0.0
- **Author:** Marek Wesołowski (WESMAR)
- **Website:** [https://kvc.pl](https://kvc.pl)
- **Contact:** [marek@wesolowski.eu.org](mailto:marek@wesolowski.eu.org)

### Technical Specifications
- **Platform:** Windows 10/11 (x64 only)
- **Language:** C (kernel driver), C++ (user-mode components)
- **Architecture:** Three-component system spanning Ring 0 and Ring 3
- **Components:**
  - **kvckbd.sys** - Kernel filter driver (Ring 0)
  - **ExplorerFrame.dll** - COM persistence layer (Ring 3)
  - **UdpLogger.exe** - Service manager and UDP logger (Ring 3)
- **Purpose:** Educational demonstration of Windows kernel programming and security research

---

## Responsible Use Guidelines

### ⚠️ CRITICAL: This is Educational Software

UdpLogger is designed exclusively for **authorized educational and research purposes**. It demonstrates advanced Windows kernel programming techniques, privilege escalation, and persistence mechanisms that should be understood by security professionals and researchers.

### Intended Use Cases

#### Legitimate Applications
- **Security Research** - Academic study of Windows kernel internals and driver development
- **Penetration Testing** - Authorized security assessments with explicit written permission
- **Educational Training** - Teaching cybersecurity, malware analysis, and defensive techniques
- **Incident Response** - Understanding attack vectors for better detection and response
- **System Programming Education** - Learning IRP-based filter drivers and WDM architecture
- **Defensive Development** - Building detection mechanisms and EDR capabilities

#### Legal Compliance

**MANDATORY REQUIREMENTS:**

Users must ensure their activities comply with all applicable laws and regulations:

- ✅ **Authorization Required** - Use ONLY on systems you own or have explicit written permission to test
- ✅ **Controlled Environment** - Deploy only in isolated lab environments with Secure Boot disabled
- ✅ **Local Laws** - Comply with computer fraud, unauthorized access, and wiretapping laws in your jurisdiction
- ✅ **Corporate Policies** - Respect organizational security policies and procedures
- ✅ **Data Protection** - Handle any captured data according to GDPR, CCPA, and privacy regulations
- ✅ **Disclosure Agreements** - Maintain signed contracts and scope documents for penetration testing

### Ethical Considerations

The open source nature of this license does not diminish the ethical responsibilities of users:

- **❌ No Malicious Use** - Never use for unauthorized access, monitoring, or malicious purposes
- **❌ No Privacy Violations** - Never deploy without explicit consent of monitored parties
- **❌ No Corporate Espionage** - Never use for competitive intelligence or trade secret theft
- **✅ Responsible Disclosure** - Report discovered vulnerabilities through appropriate channels  
- **✅ Community Benefit** - Consider contributing improvements back to the project
- **✅ Knowledge Sharing** - Use for advancing security research and education

---

## Legal Warnings and Restrictions

### Criminal and Civil Liability

**IMPORTANT:** Misuse of this software may result in severe legal consequences:

- **Criminal Charges:** Unauthorized computer access, wiretapping, identity theft
- **Civil Liability:** Privacy violations, breach of contract, tortious interference
- **Regulatory Penalties:** GDPR violations (up to €20M or 4% of revenue), CCPA fines

### Jurisdictional Considerations

This software may be subject to different legal frameworks depending on your location:

- **United States:** Computer Fraud and Abuse Act (CFAA), Wiretap Act, state laws
- **European Union:** GDPR, Computer Misuse Act (UK), national cybercrime laws
- **Poland:** Ustawa o ochronie danych osobowych, Kodeks karny Art. 267-269a
- **Other Jurisdictions:** Consult local legal counsel before use

### Author Liability Disclaimer

**The author explicitly disclaims any responsibility for misuse:**

- ❌ The author does not endorse, encourage, or support illegal activities
- ❌ The author is not responsible for any damages, legal consequences, or harm resulting from use
- ❌ The author provides no warranty regarding fitness for any particular purpose
- ✅ Users assume full legal and ethical responsibility for their actions

---

## Contributing to UdpLogger

### Community Contributions Welcome

As an open source educational project, UdpLogger benefits from community contributions:

#### Ways to Contribute
- **Code Improvements** - Bug fixes, performance enhancements, detection evasion techniques
- **Documentation** - Enhanced guides, tutorials, and technical documentation
- **Testing** - Compatibility testing across Windows versions (10, 11, Server 2019/2022)
- **Security Research** - New kernel programming techniques and methodologies
- **Educational Content** - Training materials, CTF challenges, and academic resources
- **Detection Signatures** - YARA rules, EDR signatures, SIEM detection logic

#### Contribution Guidelines
- **Code Quality** - Follow Windows Driver Kit (WDK) standards and best practices
- **Documentation** - Include comprehensive comments explaining kernel-level operations
- **Testing** - Verify changes across multiple Windows builds and configurations
- **Compatibility** - Maintain support for Windows 10/11 x64
- **Attribution** - Contributors will be acknowledged in project documentation
- **Ethical Review** - Ensure contributions maintain educational focus

### Development Resources

#### Technical Documentation
- **Architecture Overview** - Three-component system design (kernel driver, COM DLL, service)
- **API Reference** - IRP handling, WSK networking, BCD manipulation
- **Build Instructions** - WDK compilation and driver signing procedures
- **Testing Procedures** - Virtual machine setup, safe testing practices

#### Community Channels
- **Primary Contact** - [marek@wesolowski.eu.org](mailto:marek@wesolowski.eu.org)
- **Project Website** - [https://kvc.pl](https://kvc.pl)
- **Phone** - +48 607-440-283
- **Issue Reporting** - Submit bugs and feature requests via established channels

---

## Third-Party Components and Dependencies

### System Dependencies

UdpLogger relies on Microsoft Windows components and libraries:

- **Windows Driver Kit (WDK)** - Kernel-mode development framework (Microsoft license)
- **Windows SDK** - User-mode development libraries (Microsoft license)
- **Visual C++ Runtime** - Standard library components (Microsoft license)
- **Boot Configuration Data (BCD)** - Windows boot configuration system (Microsoft proprietary)

### License Compatibility

Users should be aware that while UdpLogger itself is MIT licensed, it interacts with:

- **Windows Kernel** - Proprietary Microsoft operating system
- **kbdclass.sys** - Microsoft keyboard class driver
- **TrustedInstaller** - Microsoft system protection mechanism

### Compliance Verification

When distributing UdpLogger or derivative works:

1. **Review Dependencies** - Ensure compatibility with Microsoft licensing terms
2. **Include Licenses** - Provide license texts for all third-party components  
3. **Attribution** - Properly credit Microsoft for Windows components
4. **Export Control** - Verify compliance with export regulations

---

## Technical Implementation Details

### System Requirements

**Mandatory Prerequisites:**
- ❌ **Secure Boot DISABLED** - Must be disabled in BIOS/UEFI
- ✅ **Administrator Privileges** - Required for installation
- ✅ **Test Signing Enabled** - Automatically configured by installer
- ✅ **Windows 10/11 x64** - 32-bit systems not supported

### Architecture Overview

UdpLogger implements a sophisticated three-tier architecture:

| Component | Type | Privilege Level | Function |
|-----------|------|----------------|----------|
| **kvckbd.sys** | Kernel Filter Driver | Ring 0 | IRP interception, scan code translation, WSK UDP transmission |
| **ExplorerFrame.dll** | COM In-Process Server | Ring 3 | CLSID hijacking for persistence, API hooking for stealth |
| **UdpLogger.exe** | Windows Service | Ring 3 (TI/Admin) | Installation orchestration, UDP logging, system management |

### Key Technical Features

- **IRP-Based Filtering** - Direct manipulation of kbdclass.sys IRP queues
- **WSK Networking** - Kernel-mode UDP communication via Winsock Kernel
- **Line Buffering** - Reduces network traffic by 95-98% through intelligent buffering
- **TrustedInstaller Escalation** - Token impersonation for system-level access
- **BCD Manipulation** - Direct registry modification for test signing enablement
- **CLSID Hijacking** - COM object persistence via registry redirection

---

## Version History and Development Roadmap

### Current Version: 1.0.0
- ✅ Complete three-component system implementation
- ✅ IRP-based keyboard filtering at kernel level
- ✅ UDP network communication (localhost port 31415)
- ✅ Daily log rotation with timestamped entries
- ✅ TrustedInstaller privilege escalation
- ✅ CLSID persistence mechanism
- ✅ Comprehensive CLI management interface
- ✅ Support for Windows 10/11 x64 platforms
- ✅ Polish diacritics support (ą, ć, ę, ł, ń, ó, ś, ź, ż)

### Known Limitations
- ⚠️ Requires Secure Boot disabled (hardware requirement)
- ⚠️ Test Mode watermark visible (unless suppressed by ExplorerFrame.dll)
- ⚠️ x64 only (no x86 support)
- ⚠️ Localhost UDP only (no remote logging)
- ⚠️ No encryption of captured data

### Future Development Possibilities
- **KMDF Rewrite** - Modernize driver using Kernel-Mode Driver Framework
- **Remote Logging** - Configurable network destinations beyond localhost
- **Encryption** - AES-256 encryption of keystroke data
- **Stealth Improvements** - Remove test signing requirement via alternative techniques
- **Multi-Keyboard Support** - Enhanced handling of multiple simultaneous keyboards
- **Cross-Platform** - Research Linux kernel module equivalent

---

## Security and Detection Considerations

### MITRE ATT&CK Mapping

This software implements techniques documented in the MITRE ATT&CK framework:

| Technique | ID | Implementation |
|-----------|----|--------------  |
| Boot or Logon Autostart Execution | T1547.001 | Driver service with AUTO_START |
| Event Triggered Execution: Component Object Model Hijacking | T1546.015 | CLSID registry redirection |
| Kernel Modules and Extensions | T1547.006 | Kernel driver loading |
| Input Capture: Keylogging | T1056.001 | IRP-based keyboard interception |
| Access Token Manipulation | T1134.001 | TrustedInstaller token theft |

### Detection Indicators

**File System Artifacts:**
- `C:\Windows\System32\ExpIorerFrame.dll` (note: uppercase 'I' instead of lowercase 'l')
- `C:\Windows\System32\DriverStore\FileRepository\keyboard.inf_amd64_*\kvckbd.sys`
- Log files: `%TEMP%\keyboard_log_YYYY-MM-DD.txt`

**Registry Artifacts:**
- `HKCR\CLSID\{ab0b37ec-56f6-4a0e-a8fd-7a8bf7c2da96}\InProcServer32` = ExpIorerFrame.dll
- `HKLM\SYSTEM\CurrentControlSet\Services\kvckbd`
- `HKLM\SYSTEM\CurrentControlSet\Services\UdpKeyboardLogger`
- `HKLM\BCD00000000\Objects\{GUID}\Elements\16000049` = 0x01

**Network Artifacts:**
- UDP traffic on localhost port 31415
- Regular traffic patterns correlating with typing activity

**Behavioral Indicators:**
- "Test Mode" watermark appearing on desktop
- New Windows service: UdpKeyboardLogger
- Kernel driver loaded: kvckbd
- TrustedInstaller service starts unexpectedly

### Defensive Countermeasures

**Prevention:**
- ✅ Enable Secure Boot (blocks unsigned/test-signed drivers)
- ✅ Enforce Driver Signature Enforcement (DSE)
- ✅ Monitor BCD modifications via audit policies
- ✅ Restrict TrustedInstaller token access
- ✅ Implement CLSID integrity monitoring
- ✅ Deploy file integrity monitoring (FIM) for System32

**Detection:**
- ✅ Audit registry changes: `HKLM\BCD00000000`
- ✅ Monitor service creation: `HKLM\SYSTEM\CurrentControlSet\Services`
- ✅ Network monitoring: localhost UDP traffic analysis
- ✅ EDR: Driver load events and kernel callbacks
- ✅ SIEM: TrustedInstaller impersonation detection

---

## Legal Notices

### Export Control and Compliance

This software may be subject to export control regulations in various jurisdictions:

- **United States:** Export Administration Regulations (EAR), ITAR considerations
- **European Union:** Dual-use export controls (EC Regulation 428/2009)
- **Wassenaar Arrangement:** Intrusion software controls

**User Responsibility:** You are responsible for compliance with applicable export control laws when distributing or using this software across international borders.

### Security and Safety Disclaimer

UdpLogger operates with elevated system privileges and directly manipulates kernel structures:

**⚠️ CRITICAL SAFETY WARNINGS:**

- **Test in Virtual Machines** - Always use isolated VMs (VirtualBox, VMware, Hyper-V) with snapshots
- **Disable Secure Boot** - Required for driver loading (system firmware setting)
- **Backup Critical Data** - Create full system backups before installation
- **Antivirus Exclusions** - May be necessary to prevent quarantine (understand risks)
- **BSOD Risk** - Kernel-level code can cause Blue Screen of Death if misused
- **Data Loss Risk** - Improper uninstallation may require system reinstallation

**Recommended Testing Environment:**
```
┌─────────────────────────────────────┐
│   Host System (Production)         │
│   ├─ Secure Boot: ENABLED          │
│   └─ UdpLogger: NOT INSTALLED      │
│                                     │
│   ┌───────────────────────────┐   │
│   │  VM (Testing Environment) │   │
│   │  ├─ Secure Boot: DISABLED │   │
│   │  ├─ Snapshot: Created     │   │
│   │  └─ UdpLogger: SAFE       │   │
│   └───────────────────────────┘   │
└─────────────────────────────────────┘
```

### Warranty Limitation

As specified in the MIT License terms above:

- ❌ **NO WARRANTY** - Software provided "as is" without any guarantees
- ❌ **NO LIABILITY** - Authors not liable for damages, data loss, or legal consequences
- ❌ **NO SUPPORT** - No obligation to provide updates, fixes, or technical support
- ✅ **BEST EFFORT** - Community support available on best-effort basis

---

## Contact and Support

### Primary Contact

**Marek Wesołowski (WESMAR)**  
Email: [marek@wesolowski.eu.org](mailto:marek@wesolowski.eu.org)  
Website: [https://kvc.pl](https://kvc.pl)  
Phone: +48 607-440-283  

### Business Information

**WESMAR - Marek Wesołowski**  
Address: Raabego 2b/81, 07-973 Warszawa, Poland  
Tax ID (NIP): 7991668581  
Statistical Number (REGON): 140406890  

### Support Expectations

- **Community Support** - Best-effort assistance for educational users via email
- **Bug Reports** - Timely response to legitimate security and stability issues
- **Feature Requests** - Consideration based on educational value and technical feasibility
- **Security Issues** - Responsible disclosure appreciated, acknowledgment provided
- **Commercial Inquiries** - Professional consulting services available upon request

### Response Time

- **Critical Security Issues:** 48-72 hours
- **Bug Reports:** 1-2 weeks
- **Feature Requests:** 2-4 weeks
- **General Inquiries:** 1 week

---

## Educational Disclaimer

### Statement of Purpose

UdpLogger is developed and distributed solely for **educational purposes**. It serves as:

1. **Teaching Tool** - Demonstrates advanced Windows kernel programming concepts
2. **Research Platform** - Enables study of Windows driver architecture and security
3. **Defense Training** - Helps security professionals understand attack techniques
4. **Academic Resource** - Supports university-level cybersecurity curriculum

### Not Intended For

- ❌ Unauthorized monitoring or surveillance
- ❌ Corporate espionage or competitive intelligence
- ❌ Privacy violations or wiretapping
- ❌ Malicious attacks or unauthorized access
- ❌ Production deployments without explicit consent

### Academic and Research Use

If using UdpLogger in academic research or publications:

- **Citation:** Please cite this project appropriately in academic papers
- **Acknowledgment:** Acknowledge the author in presentations and publications
- **Collaboration:** Contact author for potential research collaborations
- **Ethical Approval:** Ensure institutional review board (IRB) approval where required

---

## Conclusion

**This MIT License ensures UdpLogger remains freely available for the security research and educational community while encouraging innovation, contribution, and responsible use.**

By using, modifying, or distributing this software, you acknowledge that you have read, understood, and agree to comply with:

✅ The terms of the MIT License  
✅ All applicable laws and regul