import tensorflow as tf
from tensorflow.keras.preprocessing.image import ImageDataGenerator
from tensorflow.keras.optimizers import Adam
from tensorflow.keras.models import Sequential
from tensorflow.keras.layers import Conv2D, MaxPooling2D, Flatten, Dense, Dropout
import matplotlib.pyplot as plt
import os

# Path dataset
train_dir = r"C:\Denivo\Denivo Kren & misterius\MAN IC\SIC\Assignment_3_SIC\Computer Vision\dataset\train"
val_dir = r"C:\Denivo\Denivo Kren & misterius\MAN IC\SIC\Assignment_3_SIC\Computer Vision\dataset\val"

# Image generator dengan augmentasi
train_datagen = ImageDataGenerator(
    rescale=1./255,
    rotation_range=15,
    zoom_range=0.1,
    horizontal_flip=True
)

val_datagen = ImageDataGenerator(rescale=1./255)

# Load image dataset
train_generator = train_datagen.flow_from_directory(
    train_dir,
    target_size=(128, 128),
    batch_size=32,
    class_mode='binary'
)

val_generator = val_datagen.flow_from_directory(
    val_dir,
    target_size=(128, 128),
    batch_size=32,
    class_mode='binary'
)

# Buat model CNN sederhana
model = Sequential([
    Conv2D(32, (3, 3), activation='relu', input_shape=(128, 128, 3)),
    MaxPooling2D(2, 2),
    Conv2D(64, (3, 3), activation='relu'),
    MaxPooling2D(2, 2),
    Flatten(),
    Dense(128, activation='relu'),
    Dropout(0.3),
    Dense(1, activation='sigmoid')  # output 0 (jalan bagus) atau 1 (rusak)
])

model.compile(optimizer=Adam(learning_rate=0.0001),
              loss='binary_crossentropy',
              metrics=['accuracy'])

# Latih model
history = model.fit(
    train_generator,
    epochs=10,
    validation_data=val_generator
)

# Simpan model
model.save('model_jalan.h5')
print("✅ Model disimpan sebagai model_jalan.h5")



