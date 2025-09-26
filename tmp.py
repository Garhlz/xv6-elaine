import numpy as np

# 1. 数据准备
text = "The quick brown fox jumps" 
chars = sorted(list(set(text))) 
vocab_size = len(chars)

# 创建字符到索引的映射
char_to_ix = {ch: i for i, ch in enumerate(chars)}
ix_to_char = {i: ch for i, ch in enumerate(chars)}

# 将输入文本转换为整数序列, 目标序列是输入序列向左移动一位
inputs = [char_to_ix[ch] for ch in text]
targets = inputs[1:] + [inputs[0]] 

# 2. 初始化权重
hidden_size = 100 
learning_rate = 0.1 

# W1输入权重
W_xh = np.random.randn(hidden_size, vocab_size) * 0.01 
# W2隐藏层权重
W_hh = np.random.randn(hidden_size, hidden_size) * 0.01
# W3输出权重
W_hy = np.random.randn(vocab_size, hidden_size) * 0.01
# 偏置项
b_h = np.zeros((hidden_size, 1))
b_y = np.zeros((vocab_size, 1))

# 3. 前向传播
xs, hs, ys, ps = {}, {}, {}, {}
hs[-1] = np.zeros((hidden_size, 1))
total_loss = 0

# 遍历输入序列计算前向传播结果
for t in range(len(inputs)):
    xs[t] = np.zeros((vocab_size, 1))
    xs[t][inputs[t]] = 1
    
    hs[t] = np.tanh(W_xh @ xs[t] + W_hh @ hs[t-1] + b_h)
    ys[t] = W_hy @ hs[t] + b_y
    ps[t] = np.exp(ys[t]) / np.sum(np.exp(ys[t]))
    total_loss += -np.log(ps[t][targets[t], 0])

# 4. 梯度计算
dW_xh, dW_hh, dW_hy = np.zeros_like(W_xh), np.zeros_like(W_hh), np.zeros_like(W_hy)
db_h, db_y = np.zeros_like(b_h), np.zeros_like(b_y)
dh_next = np.zeros_like(hs[0])

# 从最后一个时间步开始，反向遍历序列
for t in reversed(range(len(inputs))):
    dy = np.copy(ps[t])
    dy[targets[t]] -= 1 

    # 计算输出层梯度
    dW_hy += dy @ hs[t].T
    db_y += dy
    
    # 计算隐藏层梯度
    dh = W_hy.T @ dy + dh_next
    dh_raw = (1 - hs[t] * hs[t]) * dh # tanh 的反向传播
    
    # 计算输入层和隐藏层权重的梯度
    dW_xh += dh_raw @ xs[t].T
    dW_hh += dh_raw @ hs[t-1].T
    db_h += dh_raw
    
    # 更新 dh_next 以传递到上一个时间步
    dh_next = W_hh.T @ dh_raw

# 5. 更新权重 ---
# 使用梯度下降法更新所有权重和偏置
W_xh -= learning_rate * dW_xh
W_hh -= learning_rate * dW_hh
W_hy -= learning_rate * dW_hy
b_h -= learning_rate * db_h
b_y -= learning_rate * db_y