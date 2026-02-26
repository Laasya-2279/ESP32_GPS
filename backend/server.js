const express = require('express');
const cors = require('cors');
const path = require('path');

const app = express();
const PORT = process.env.PORT || 3000;

// Middleware
app.use(cors());
app.use(express.json());
app.use(express.static(path.join(__dirname, '../frontend')));

// Routes
app.get('/api/health', (req, res) => {
  res.json({ status: 'Server is running' });
});

app.post('/api/gps', (req, res) => {
  const { latitude, longitude, timestamp } = req.body;
  console.log(`GPS Data - Lat: ${latitude}, Lon: ${longitude}, Time: ${timestamp}`);
  res.json({ success: true, message: 'GPS data received' });
});

app.get('/', (req, res) => {
  res.sendFile(path.join(__dirname, '../frontend/gps-sender.html'));
});

// Start server
app.listen(PORT, () => {
  console.log(`ESP32 GPS Demo server running on http://localhost:${PORT}`);
});
