using System.Reflection;
using Sims3.SimIFace;

// TuningControl.TuneAssemblies skips any assembly without this attribute, and tuning is what runs Instantiator's
// static constructor (which applies the fixes).
[assembly: Tunable]
[assembly: AssemblyTitle("Ts3Fixes")]
[assembly: AssemblyVersion("0.1.0.0")]
