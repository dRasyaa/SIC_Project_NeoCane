from flask import Flask, request, jsonify
import base64
import io
from PIL import Image
import numpy as np
import tensorflow as tf
from datetime import datetime
import os
import shutil
import requests

latest_status = {"status": "unknown", "timestamp": "-", "image": None}

# Folder simpan foto
PHOTO_FOLDER = "saved_photos"
os.makedirs(PHOTO_FOLDER, exist_ok=True)

# Inisialisasi Flask
app = Flask(__name__)

# Load TFLite model
interpreter = tf.lite.Interpreter(model_path="Projek\model_jalan.tflite")
interpreter.allocate_tensors()

input_details = interpreter.get_input_details()
output_details = interpreter.get_output_details()

# Ukuran input model
target_size = input_details[0]['shape'][1:3]  # e.g. (128, 128)

# Fungsi kirim ke Ubidots
def send_to_ubidots(status):
    token = "BBUS-dUnnmdDGegd40VNGBKuCOnpvAbO9eJ"
    device = "neocane-dashboard"
    variable = "ai_vision"
    value = 1 if status == "jalan rusak" else 0

    url = f"https://industrial.api.ubidots.com/api/v1.6/devices/{device}/{variable}/values"
    headers = {"X-Auth-Token": token, "Content-Type": "application/json"}
    payload = {"value": value}

    try:
        response = requests.post(url, headers=headers, json=payload)
        print(f"✅ Data status '{status}' dikirim ke Ubidots: {response.status_code}")
    except Exception as e:
        print(f"❌ Gagal kirim ke Ubidots: {e}")

# Fungsi preprocessing gambar
def preprocess_image(img_base64):
    img_data = base64.b64decode(img_base64)
    img = Image.open(io.BytesIO(img_data)).convert('RGB')
    img = img.resize(target_size)
    img = np.array(img, dtype=np.float32) / 255.0
    img = np.expand_dims(img, axis=0)
    return img

@app.route('/predict', methods=['POST'])
def predict():
    try:
        data = request.get_json()
        img_base64 = data['image']
        image = preprocess_image(img_base64)

        interpreter.set_tensor(input_details[0]['index'], image)
        interpreter.invoke()
        output_data = interpreter.get_tensor(output_details[0]['index'])

        result = float(output_data[0][0])  # sigmoid output

        if result > 0.5:
            status = "jalan rusak"
        else:
            status = "aman"

        send_to_ubidots(status) 

        return jsonify({"status": status})

    except Exception as e:
        return jsonify({"error": str(e)}), 500
    
@app.route('/distance', methods=['POST'])
def receive_distance():
    try:
        data = request.get_json()
        distance_front = data.get('front')
        distance_left = data.get('left')
        distance_right = data.get('right')

        latest_status["distance"] = {
            "front": distance_front,
            "left": distance_left,
            "right": distance_right
        }
        latest_status["timestamp"] = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

        return jsonify({"message": "Objek Terdeteksi", "data": latest_status["distance"]})

    except Exception as e:
        return jsonify({"error": str(e)}), 500


@app.route('/save-photo', methods=['POST'])
def save_photo():
    try:
        data = request.get_json()
        if not data or 'image' not in data:
            return jsonify({"error": "image not found"}), 400

        image_data = data['image']
        image_bytes = base64.b64decode(image_data)

        # ROTASI FOTO 5 TERAKHIR
        for i in range(1, 5):  # photo_2 → photo_1, dst.
            src = os.path.join(PHOTO_FOLDER, f"photo_{i+1}.jpg")
            dst = os.path.join(PHOTO_FOLDER, f"photo_{i}.jpg")
            if os.path.exists(src):
                shutil.move(src, dst)

        # Simpan foto baru sebagai photo_5.jpg
        new_photo_path = os.path.join(PHOTO_FOLDER, "photo_5.jpg")
        with open(new_photo_path, "wb") as f:
            f.write(image_bytes)

        print("✅ Foto disimpan:", new_photo_path)
        return jsonify({"status": "foto disimpan"})

    except Exception as e:
        return jsonify({"error": str(e)}), 500

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5500)