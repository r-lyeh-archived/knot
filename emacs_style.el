;;; emacs_style.el --- 

;; Copyright 2014 Gaspar Fernández
;;
;; Author: Gaspar Fernández <blakeyed@totaki.com>
;; Version: $Id: emacs_style.el,v 0.0 2014/03/24 16:36:30 gaspy Exp $
;; Keywords: emacs, style, indent, spaces
;; X-URL: https://github.com/blakeyed/knot

;; This program is free software; you can redistribute it and/or modify
;; it under the terms of the GNU General Public License as published by
;; the Free Software Foundation; either version 2, or (at your option)
;; any later version.
;;
;; This program is distributed in the hope that it will be useful,
;; but WITHOUT ANY WARRANTY; without even the implied warranty of
;; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;; GNU General Public License for more details.
;;
;; You should have received a copy of the GNU General Public License
;; along with this program; if not, write to the Free Software
;; Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

;;; Commentary:

;; Basic coding style for EMACS

;; Put this file into your load-path and the following into your ~/.emacs:
;;   (require 'emacs_style)

;;; Code:

(setq tab-width 4)
(setq indent-tabs-mode nil)

(setq c-default-style "linux")
(setq c-basic-offset 4)
(c-set-offset 'substatement-open 0)
(c-set-offset 'statement-cont 0)
(c-set-offset 'brace-list-open 0)
(c-set-offset 'statement-block-intro 4)

;;;;##########################################################################
;;;;  User Options, Variables
;;;;##########################################################################





;;; emacs_style.el ends here
