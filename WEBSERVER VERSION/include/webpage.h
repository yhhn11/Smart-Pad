#ifndef WEBPAGE_H
#define WEBPAGE_H

const char webpage[] PROGMEM = R"=====(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>SmartPad Controller</title>
    <style>
        body {
            font-family: Arial, sans-serif;
            padding: 20px;
            max-width: 800px;
            margin: 0 auto;
            background-color: #000000;
            color: #ffffff;
        }
        
        .container {
            background-color: #1a1a1a;
            border: 1px solid #ff6600;
            padding: 20px;
            border-radius: 8px;
            box-shadow: 0 2px 4px rgba(0,0,0,0.1);
        }
        
        .setup-form {
            margin-bottom: 20px;
        }
        
        .form-group {
            margin-bottom: 15px;
        }
        
        label {
            display: block;
            margin-bottom: 5px;
            font-weight: bold;
            color: #ffffff;
        }
        
        select {
            background-color: #333333;
            color: #ffffff;
            border: 1px solid #ff6600;
            width: 100%;
            padding: 8px;
            margin-bottom: 10px;
            border-radius: 4px;
            font-size: 16px;
        }

        input[type="range"] {
            width: 100%;
            height: 25px;
            -webkit-appearance: none;
            background: #333333;
            border-radius: 5px;
            outline: none;
            opacity: 0.7;
            -webkit-transition: .2s;
            transition: opacity .2s;
        }

        input[type="range"]:hover {
            opacity: 1;
        }

        input[type="range"]::-webkit-slider-thumb {
            -webkit-appearance: none;
            appearance: none;
            width: 25px;
            height: 25px;
            background: #ff6600;
            cursor: pointer;
            border-radius: 50%;
        }

        input[type="range"]::-moz-range-thumb {
            width: 25px;
            height: 25px;
            background: #ff6600;
            cursor: pointer;
            border-radius: 50%;
        }

        .form-group label {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 10px;
        }

        .form-group label span {
            background: #ff6600;
            color: white;
            padding: 2px 8px;
            border-radius: 4px;
            font-size: 14px;
        }
        
        button {
            background-color: #ff6600;
            color: white;
            padding: 10px 20px;
            border: none;
            border-radius: 4px;
            cursor: pointer;
            font-size: 16px;
            width: 100%;
        }
        
        button:disabled {
            background-color: #ccc;
            cursor: not-allowed;
        }
        
        .current-setup {
            background-color: #1a1a1a;
            padding: 15px;
            margin: 20px 0;
            border-radius: 4px;
            border: 1px solid #ff6600;
            color: #ffffff;
        }

        .setup-info p {
            margin: 8px 0;
            font-size: 16px;
            color: #ffffff;
        }

        .status {
            padding: 10px;
            margin: 10px 0;
            border-radius: 4px;
            background-color: #1a1a1a;
            border: 1px solid #ff6600;
            color: #ff6600;
        }

        .readings {
            background-color: #1a1a1a;
            padding: 15px;
            margin-top: 20px;
            border: 1px solid #ff6600;
            border-radius: 4px;
            color: #ffffff;
        }

        .channels-grid {
            display: grid;
            grid-template-columns: repeat(2, 1fr);
            gap: 10px;
            margin-top: 15px;
        }

        .channel-reading {
            background-color: #333333;
            padding: 10px;
            border-radius: 4px;
            border: 1px solid #ff6600;
            font-family: monospace;
            font-size: 16px;
            color: #ffffff;
        }

        h1, h3 {
            color: #ffffff;
            margin-bottom: 20px;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>SmartPad Controller</h1>
        
        <div class="setup-form">
            <div class="form-group">
                <label for="gain">Gain:</label>
                <select id="gain" required>
                    <option value="1">1x</option>
                    <option value="2">2x</option>
                    <option value="4">4x</option>
                    <option value="8">8x</option>
                    <option value="16">16x</option>
                    <option value="32">32x</option>
                    <option value="64">64x</option>
                    <option value="128">128x</option>
                    <option value="256">256x</option>
                    <option value="512">512x</option>
                </select>
            </div>
            
            <div class="form-group">
                <label for="current">LED Current: <span id="currentValue">100</span> mA</label>
                <input type="range" id="current" min="4" max="258" value="100" 
                    oninput="document.getElementById('currentValue').textContent = this.value">
            </div>
            
            <div class="form-group">
                <label for="samples">Number of Samples: <span id="samplesValue">1</span></label>
                <input type="range" id="samples" min="1" max="22" value="1" 
                    oninput="document.getElementById('samplesValue').textContent = this.value">
            </div>
            
            <div class="form-group">
                <label for="sampleType">Sample Type:</label>
                <select id="sampleType" required>
                    <option value="0">Raw</option>
                    <option value="1">Absorbance</option>
                </select>
            </div>
            
            <button type="button" id="startButton" onclick="startMeasurement()">Send Setup</button>
        </div>

        <div class="current-setup" id="currentSetup" style="display: none;">
            <h3>Current Setup Configuration:</h3>
            <div class="setup-info">
                <p>Number of samples: <span id="setupSamples"></span></p>
                <p>Sample type: <span id="setupType"></span></p>
                <p>Gain: <span id="setupGain"></span></p>
                <p>LED current: <span id="setupCurrent"></span> mA</p>
            </div>
        </div>

        <div class="readings" id="readingsDisplay" style="display: none;">
            <h3>Results</h3>
            <p><strong>Sample number: <span id="currentSample">0</span></strong></p>
            <div class="channels-grid" id="channelReadings">
                <div class="channel-reading">415nm: <span id="channel415"></span></div>
                <div class="channel-reading">445nm: <span id="channel445"></span></div>
                <div class="channel-reading">480nm: <span id="channel480"></span></div>
                <div class="channel-reading">515nm: <span id="channel515"></span></div>
                <div class="channel-reading">555nm: <span id="channel555"></span></div>
                <div class="channel-reading">590nm: <span id="channel590"></span></div>
                <div class="channel-reading">630nm: <span id="channel630"></span></div>
                <div class="channel-reading">680nm: <span id="channel680"></span></div>
            </div>
        </div>
    </div>

    <script>
        let measuring = false;
        
        async function startMeasurement() {
            const gain = document.getElementById('gain').value;
            const current = document.getElementById('current').value;
            const samples = document.getElementById('samples').value;
            const sampleType = document.getElementById('sampleType').value;

            try {
                document.getElementById('startButton').disabled = true;
                updateSetupDisplay();
                
                const response = await fetch(
                    `/setup?gain=${gain}&current=${current}&samples=${samples}&sampleType=${sampleType}`
                );
                
                if (!response.ok) {
                    throw new Error('Erro na configuração');
                }

                measuring = true;
                document.getElementById('readingsDisplay').style.display = 'block';
                pollData();

            } catch (error) {
                document.getElementById('startButton').disabled = false;
            }
        }

        async function pollData() {
            if (!measuring) return;

            try {
                const response = await fetch('/data');
                
                if (response.status === 202) {
                    setTimeout(pollData, 1000);
                    return;
                }

                const data = await response.json();
                
                document.getElementById('currentSample').textContent = data.sampleNumber;
                
                const wavelengths = [415, 445, 480, 515, 555, 590, 630, 680];
                wavelengths.forEach((wavelength, index) => {
                    document.getElementById(`channel${wavelength}`).textContent = 
                        data.channels[index].toFixed(2);
                });
                
                if (data.isComplete) {
                    setTimeout(() => {
                        if (data.sampleNumber < document.getElementById('samples').value) {
                            setTimeout(pollData, 1000);
                        } else {
                            measuring = false;
                            document.getElementById('startButton').disabled = false;
                        }
                    }, 10000);
                } else {
                    setTimeout(pollData, 1000);
                }

            } catch (error) {
                measuring = false;
                document.getElementById('startButton').disabled = false;
            }
        }

        function updateSetupDisplay() {
            const setup = document.getElementById('currentSetup');
            setup.style.display = 'block';
            
            document.getElementById('setupSamples').textContent = document.getElementById('samples').value;
            document.getElementById('setupType').textContent = 
                document.getElementById('sampleType').value === '0' ? 'Raw' : 'Absorbance';
            document.getElementById('setupGain').textContent = document.getElementById('gain').value + 'x';
            document.getElementById('setupCurrent').textContent = document.getElementById('current').value;
        }
    </script>
</body>
</html>
)=====";

#endif