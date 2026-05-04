param([string]$JuceDir="")
$Args = @("-S", ".", "-B", "build", "-DCMAKE_BUILD_TYPE=Release")
if ($JuceDir -ne "") { $Args += "-DJUCE_DIR=$JuceDir" }
cmake @Args
cmake --build build --config Release --target FreeVox8_All
Write-Host "FreeVox8 artifacts: build/FreeVox8_artefacts/Release"
