"""
Trains a tiny digit-classifier neural net (64 -> 16 -> 10) on scikit-learn's
built-in handwritten digits dataset (8x8 images, the same task as MNIST,
just lower resolution -- chosen because it ships with scikit-learn itself,
no external dataset download required).

Pure numpy, no deep learning framework -- simple enough to hand-translate
into C for the kernel's inference program later.
"""
import numpy as np
from sklearn.datasets import load_digits
from sklearn.model_selection import train_test_split

np.random.seed(42)

digits = load_digits()
X = digits.data.astype(np.float64) / 16.0   # normalize pixels to [0, 1]
y = digits.target

X_train, X_test, y_train, y_test = train_test_split(
    X, y, test_size=0.15, random_state=42, stratify=y
)

INPUT_SIZE = 64
HIDDEN_SIZE = 16
OUTPUT_SIZE = 10

W1 = np.random.randn(INPUT_SIZE, HIDDEN_SIZE) * np.sqrt(2.0 / INPUT_SIZE)
b1 = np.zeros(HIDDEN_SIZE)
W2 = np.random.randn(HIDDEN_SIZE, OUTPUT_SIZE) * np.sqrt(2.0 / HIDDEN_SIZE)
b2 = np.zeros(OUTPUT_SIZE)

def relu(x):
    return np.maximum(0, x)

def relu_deriv(x):
    return (x > 0).astype(np.float64)

def softmax(x):
    e = np.exp(x - np.max(x, axis=1, keepdims=True))
    return e / np.sum(e, axis=1, keepdims=True)

def one_hot(y, num_classes):
    out = np.zeros((len(y), num_classes))
    out[np.arange(len(y)), y] = 1
    return out

Y_train = one_hot(y_train, OUTPUT_SIZE)

lr = 0.5
epochs = 400
n = X_train.shape[0]

for epoch in range(epochs):
    z1 = X_train @ W1 + b1
    a1 = relu(z1)
    z2 = a1 @ W2 + b2
    a2 = softmax(z2)

    loss = -np.mean(np.sum(Y_train * np.log(a2 + 1e-9), axis=1))

    dz2 = (a2 - Y_train) / n
    dW2 = a1.T @ dz2
    db2 = np.sum(dz2, axis=0)

    da1 = dz2 @ W2.T
    dz1 = da1 * relu_deriv(z1)
    dW1 = X_train.T @ dz1
    db1 = np.sum(dz1, axis=0)

    W1 -= lr * dW1
    b1 -= lr * db1
    W2 -= lr * dW2
    b2 -= lr * db2

    if epoch % 50 == 0 or epoch == epochs - 1:
        pred = np.argmax(a2, axis=1)
        acc = np.mean(pred == y_train)
        print(f"epoch {epoch:4d}  loss {loss:.4f}  train_acc {acc:.4f}")

# Evaluate on held-out test set
z1 = X_test @ W1 + b1
a1 = relu(z1)
z2 = a1 @ W2 + b2
test_pred = np.argmax(z2, axis=1)  # raw logits, no softmax needed for argmax
test_acc = np.mean(test_pred == y_test)
print(f"\nFinal test accuracy: {test_acc:.4f} ({int(test_acc*len(y_test))}/{len(y_test)})")

np.savez("model.npz",
         W1=W1, b1=b1, W2=W2, b2=b2,
         X_test=X_test, y_test=y_test)
print("Saved model + test data to model.npz")