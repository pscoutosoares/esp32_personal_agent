import GoogleSpeechToText from "./Services/SpeechRecognition/google/index.ts";
import { startDeviceProcess } from "./device/esp32.ts";

async function main() {
  const SpeechObject = new GoogleSpeechToText();
  SpeechObject.createNewStream();
  SpeechObject.startListening((text) => {
    console.log("Frase completa:", text);
  });
  startDeviceProcess(SpeechObject);
}

main();
