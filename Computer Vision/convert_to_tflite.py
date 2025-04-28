import tensorflow as tf

# Load model
model = tf.keras.models.load_model(
    r"C:\Denivo\Denivo Kren & misterius\MAN IC\SIC\Assignment_3_SIC\Computer Vision\model_jalan.h5"
)

# Konversi ke TensorFlow Lite
converter = tf.lite.TFLiteConverter.from_keras_model(model)
tflite_model = converter.convert()

# Simpan ke file
with open('model_jalan.tflite', 'wb') as f:
    f.write(tflite_model)

print("Model berhasil dikonversi ke model_jalan.tflite")