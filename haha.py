import csv
import matplotlib.pyplot as plt
x = []
y = []
count = 0
with open('cwnd_and_t.csv', 'rb') as csvfile:
  spamreader = csv.reader(csvfile, delimiter=',')
  for row in spamreader:
    if len(row) == 2 and len(row[0]) > 0 and len(row[1]) >0:
      x.append(int(row[0]))
      y.append(int(row[1]))

plt.plot(range(len(y)), y)

plt.show()
