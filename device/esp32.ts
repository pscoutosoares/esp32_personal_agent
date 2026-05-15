import { SerialPort } from "serialport";

const port = new SerialPort({
  path: "COM4",
  baudRate: 921600,
});

function device() {
  let capturando = false;
}
