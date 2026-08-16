#ifndef WEBPAGE_H
#define WEBPAGE_H

const char webpage[] = R"=====(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Smart Sense</title>
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
        select, input[type="text"] {
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
        h1, h3 {
            color: #ffffff;
            margin-bottom: 20px;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>Smart Sense</h1>
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
                <label for="concentration1">First Concentration :</label>
                <input type="text" id="concentration1" placeholder="Enter first concentration value">
            </div>
            <div class="form-group">
                <label for="concentration2">Second Concentration :</label>
                <input type="text" id="concentration2" placeholder="Enter second concentration value">
            </div>
            <div class="form-group">
                <label for="concentration3">Third Concentration :</label>
                <input type="text" id="concentration3" placeholder="Enter third concentration value">
            </div>
            <div class="form-group">
                <label for="concentration4">Fourth Concentration :</label>
                <input type="text" id="concentration4" placeholder="Enter fourth concentration value">
            </div>
            <div class="form-group">
                <label for="concentration5">Fifth Concentration :</label>
                <input type="text" id="concentration5" placeholder="Enter fifth concentration value">
            </div>
            <div class="form-group">
                <label for="channels">Channel selection :</label>
                <select id="channels" multiple size="8" style="height:auto;">
                    <option value="415">415 nm</option>
                    <option value="445">445 nm</option>
                    <option value="480">480 nm</option>
                    <option value="515">515 nm</option>
                    <option value="555">555 nm</option>
                    <option value="590">590 nm</option>
                    <option value="630">630 nm</option>
                    <option value="680">680 nm</option>
                </select>
            </div>
            <button type="button" id="startButton" onclick="startMeasurement()">Send Setup</button>
        </div>
        <div class="current-setup" id="currentSetup" style="display: none;">
            <h3>Current Setup Configuration:</h3>
            <div class="setup-info">
                <p>Gain: <span id="setupGain"></span></p>
                <p>LED current: <span id="setupCurrent"></span> mA</p>
                <p>Concentration values: <span id="setupConcentration"></span></p>
                <p>Channels: <span id="setupChannels"></span></p>
            </div>
        </div>
    </div>
    <script>
        async function startMeasurement() {
            const gain = document.getElementById('gain').value;
            const current = document.getElementById('current').value;
            const concentration1 = document.getElementById('concentration1').value;
            const concentration2 = document.getElementById('concentration2').value;
            const concentration3 = document.getElementById('concentration3').value;
            const concentration4 = document.getElementById('concentration4').value;
            const concentration5 = document.getElementById('concentration5').value;
            // Capture the selected channels
            const channels = Array.from(document.getElementById('channels').selectedOptions).map(opt => opt.value).join(',');

            try {
                document.getElementById('startButton').disabled = true;
                updateSetupDisplay();

                const response = await fetch(
                    `/setup?gain=${gain}&current=${current}&concentration1=${concentration1}&concentration2=${concentration2}&concentration3=${concentration3}&concentration4=${concentration4}&concentration5=${concentration5}&channels=${channels}`
                );

                if (!response.ok) {
                    throw new Error('Setup error');
                }

            } catch (error) {
                document.getElementById('startButton').disabled = false;
            }
        }

        function updateSetupDisplay() {
            const setup = document.getElementById('currentSetup');
            setup.style.display = 'block';
            document.getElementById('setupGain').textContent = document.getElementById('gain').value + 'x';
            document.getElementById('setupCurrent').textContent = document.getElementById('current').value;
            document.getElementById('setupConcentration').textContent =
                document.getElementById('concentration1').value + ", " +
                document.getElementById('concentration2').value + ", " +
                document.getElementById('concentration3').value + ", " +
                document.getElementById('concentration4').value + ", " +
                document.getElementById('concentration5').value;
            document.getElementById('setupChannels').textContent =
                Array.from(document.getElementById('channels').selectedOptions).map(opt => opt.text).join(', ');
        }
    </script>
</body>
</html>

)=====";

#endif