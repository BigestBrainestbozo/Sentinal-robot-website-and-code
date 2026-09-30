const heldKeys = [];
const commandHistory = [];

const statusElement = document.getElementById("status");
const commandIconElement = document.getElementById("command-icon");
const commandCard = document.querySelector(".current-command-card");
const historyElement = document.getElementById("command-history");

const joystick = document.getElementById("joystick");
const joystickKnob = document.getElementById("joystick-knob");
const emergencyStopButton = document.getElementById("emergency-stop");

const currentDateElement = document.getElementById("current-date");
const currentTimeElement = document.getElementById("current-time");

let joystickActive = false;
let currentCommand = "";
let audioContext = null;

const commandSymbols = {
    STOP: "■",
    FORWARD: "↑",
    BACKWARD: "↓",
    LEFT: "←",
    RIGHT: "→",
    FORWARD_LEFT: "↖",
    FORWARD_RIGHT: "↗",
    BACKWARD_LEFT: "↙",
    BACKWARD_RIGHT: "↘"
};

function updateClock() {
    const now = new Date();

    currentDateElement.textContent = now.toLocaleDateString("en-US", {
        month: "short",
        day: "numeric",
        year: "numeric"
    });

    currentTimeElement.textContent = now.toLocaleTimeString("en-US");
}

function getAudioContext() {
    if (!audioContext) {
        const AudioContextClass =
            window.AudioContext || window.webkitAudioContext;

        if (AudioContextClass) {
            audioContext = new AudioContextClass();
        }
    }

    if (audioContext && audioContext.state === "suspended") {
        audioContext.resume();
    }

    return audioContext;
}

function playTone(frequency, duration, volume) {
    const context = getAudioContext();

    if (!context) {
        return;
    }

    const oscillator = context.createOscillator();
    const gain = context.createGain();

    oscillator.connect(gain);
    gain.connect(context.destination);

    oscillator.frequency.value = frequency;
    gain.gain.setValueAtTime(volume, context.currentTime);

    gain.gain.exponentialRampToValueAtTime(
        0.001,
        context.currentTime + duration
    );

    oscillator.start();
    oscillator.stop(context.currentTime + duration);
}

function playCommandSound() {
    playTone(520, 0.07, 0.04);
}

function playEmergencySound() {
    playTone(180, 0.35, 0.12);
}

function animateCommand() {
    commandCard.classList.remove("command-change");
    commandIconElement.classList.remove("command-change");

    void commandCard.offsetWidth;
    void commandIconElement.offsetWidth;

    commandCard.classList.add("command-change");
    commandIconElement.classList.add("command-change");
}

function sendCommand(command, emergency = false) {
    if (currentCommand === command && !emergency) {
        return;
    }

    currentCommand = command;

    const symbol = commandSymbols[command] || "■";

    commandIconElement.textContent = symbol;
    statusElement.textContent = command.replaceAll("_", " ");

    animateCommand();
    addCommandToHistory(command);

    if (emergency) {
        playEmergencySound();
    } else {
        playCommandSound();
    }
}

function addCommandToHistory(command) {
    if (commandHistory[0] === command) {
        return;
    }

    commandHistory.unshift(command);

    if (commandHistory.length > 5) {
        commandHistory.pop();
    }

    historyElement.innerHTML = "";

    for (const item of commandHistory) {
        const listItem = document.createElement("li");
        const symbol = commandSymbols[item] || "■";

        listItem.textContent =
            `${symbol} ${item.replaceAll("_", " ")}`;

        historyElement.appendChild(listItem);
    }
}

function clearButtonHighlights() {
    document.querySelectorAll(".control-button").forEach(button => {
        button.classList.remove("active");
    });
}

function highlightButton(className) {
    const button = document.querySelector("." + className);

    if (button) {
        button.classList.add("active");
    }
}

function updateMovement() {
    clearButtonHighlights();

    const w = heldKeys.includes("w");
    const a = heldKeys.includes("a");
    const s = heldKeys.includes("s");
    const d = heldKeys.includes("d");

    if (w && a) {
        highlightButton("forward");
        highlightButton("left");
        sendCommand("FORWARD_LEFT");
    } else if (w && d) {
        highlightButton("forward");
        highlightButton("right");
        sendCommand("FORWARD_RIGHT");
    } else if (s && a) {
        highlightButton("backward");
        highlightButton("left");
        sendCommand("BACKWARD_LEFT");
    } else if (s && d) {
        highlightButton("backward");
        highlightButton("right");
        sendCommand("BACKWARD_RIGHT");
    } else if (w) {
        highlightButton("forward");
        sendCommand("FORWARD");
    } else if (a) {
        highlightButton("left");
        sendCommand("LEFT");
    } else if (s) {
        highlightButton("backward");
        sendCommand("BACKWARD");
    } else if (d) {
        highlightButton("right");
        sendCommand("RIGHT");
    } else {
        sendCommand("STOP");
    }
}

document.addEventListener("keydown", event => {
    const key = event.key.toLowerCase();

    if (event.code === "Space") {
        event.preventDefault();

        heldKeys.length = 0;
        clearButtonHighlights();
        resetJoystick();
        sendCommand("STOP");

        return;
    }

    if (!["w", "a", "s", "d"].includes(key)) {
        return;
    }

    if (!heldKeys.includes(key)) {
        heldKeys.push(key);
    }

    updateMovement();
});

document.addEventListener("keyup", event => {
    const key = event.key.toLowerCase();
    const keyIndex = heldKeys.indexOf(key);

    if (keyIndex !== -1) {
        heldKeys.splice(keyIndex, 1);
    }

    updateMovement();
});

document.querySelectorAll(".control-button").forEach(button => {
    const command = button.dataset.command;

    button.addEventListener("pointerdown", event => {
        event.preventDefault();

        if (command === "STOP") {
            heldKeys.length = 0;
            clearButtonHighlights();
            resetJoystick();

            button.classList.add("active");
            sendCommand("STOP");

            return;
        }

        button.classList.add("active");
        sendCommand(command);
    });

    const stopButtonMovement = () => {
        button.classList.remove("active");
        sendCommand("STOP");
    };

    button.addEventListener("pointerup", stopButtonMovement);
    button.addEventListener("pointerleave", stopButtonMovement);
    button.addEventListener("pointercancel", stopButtonMovement);
});

function updateJoystickPosition(event) {
    const box = joystick.getBoundingClientRect();

    const centerX = box.left + box.width / 2;
    const centerY = box.top + box.height / 2;

    let x = event.clientX - centerX;
    let y = event.clientY - centerY;

    const maximumDistance = box.width / 2 - 45;
    const distance = Math.sqrt(x * x + y * y);

    if (distance > maximumDistance) {
        x = x / distance * maximumDistance;
        y = y / distance * maximumDistance;
    }

    joystickKnob.style.transform =
        `translate(calc(-50% + ${x}px), calc(-50% + ${y}px))`;

    chooseJoystickCommand(x, y);
}

function chooseJoystickCommand(x, y) {
    const deadZone = 20;

    if (
        Math.abs(x) < deadZone &&
        Math.abs(y) < deadZone
    ) {
        sendCommand("STOP");
        return;
    }

    const angle = Math.atan2(-y, x) * 180 / Math.PI;

    if (angle >= 67.5 && angle < 112.5) {
        sendCommand("FORWARD");
    } else if (angle >= 22.5 && angle < 67.5) {
        sendCommand("FORWARD_RIGHT");
    } else if (angle >= -22.5 && angle < 22.5) {
        sendCommand("RIGHT");
    } else if (angle >= -67.5 && angle < -22.5) {
        sendCommand("BACKWARD_RIGHT");
    } else if (angle >= -112.5 && angle < -67.5) {
        sendCommand("BACKWARD");
    } else if (angle >= -157.5 && angle < -112.5) {
        sendCommand("BACKWARD_LEFT");
    } else if (angle >= 157.5 || angle < -157.5) {
        sendCommand("LEFT");
    } else {
        sendCommand("FORWARD_LEFT");
    }
}

function resetJoystick() {
    joystickActive = false;

    if (joystick) {
        joystick.classList.remove("active");
    }

    if (joystickKnob) {
        joystickKnob.style.transform =
            "translate(-50%, -50%)";
    }
}

if (joystick && joystickKnob) {
    joystick.addEventListener("pointerdown", event => {
        joystickActive = true;
        joystick.classList.add("active");
        joystick.setPointerCapture(event.pointerId);

        updateJoystickPosition(event);
    });

    joystick.addEventListener("pointermove", event => {
        if (joystickActive) {
            updateJoystickPosition(event);
        }
    });

    const stopJoystick = () => {
        resetJoystick();
        sendCommand("STOP");
    };

    joystick.addEventListener("pointerup", stopJoystick);
    joystick.addEventListener("pointercancel", stopJoystick);
    joystick.addEventListener("lostpointercapture", stopJoystick);
}

emergencyStopButton.addEventListener("click", () => {
    heldKeys.length = 0;

    clearButtonHighlights();
    resetJoystick();
    sendCommand("STOP", true);

    if (navigator.vibrate) {
        navigator.vibrate([120, 60, 120]);
    }
});

document.addEventListener("visibilitychange", () => {
    if (document.hidden) {
        heldKeys.length = 0;
        clearButtonHighlights();
        resetJoystick();
        sendCommand("STOP");
    }
});

window.addEventListener("blur", () => {
    heldKeys.length = 0;
    clearButtonHighlights();
    resetJoystick();
    sendCommand("STOP");
});

updateClock();
setInterval(updateClock, 1000);

sendCommand("STOP");