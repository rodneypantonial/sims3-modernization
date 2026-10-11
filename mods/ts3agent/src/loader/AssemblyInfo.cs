using System.Reflection;
using Sims3.SimIFace;

// TuningControl.TuneAssemblies skips any assembly without this attribute, and
// tuning is what triggers Instantiator's static constructor.
[assembly: Tunable]
[assembly: AssemblyTitle("Ts3Agent")]
[assembly: AssemblyVersion("0.1.0.0")]
